#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include<soc/mt3620_i2cs.h>
#include <applibs/log.h>
#include <applibs/gpio.h>
#include <applibs/storage.h>
#include <curl/curl.h>
#include "dhtlib.h"
#include <time.h>


#define I2C_STRUCTS_VERSION 1
#define I2C_ID MT3620_I2C_ISU2
#include <applibs/i2c.h>

int i2cFd;

#include"sd1306.h"

float charsToFloat(char* chars, int startindex)
{
	float f;
	memcpy(&f, &chars[startindex], sizeof(4));
	return f;
}

struct WriteThis {
	const char* readptr;
	int sizeleft;
};

static size_t read_callback(void* ptr, size_t size, size_t nmemb, void* userp)
{
	struct WriteThis* pooh = (struct WriteThis*)userp;

	if (size * nmemb >= pooh->sizeleft) {
		*(char*)ptr = pooh->readptr[0]; /* copy one single byte */
		pooh->readptr++;                 /* advance pointer */
		pooh->sizeleft--;                /* less data left */
		return 1;                        /* we return 1 byte at a time! */
	}

	return 0;                          /* no more data left to deliver */
}

int post(char* url, char* data)
{
	CURL* curl;
	CURLcode res;

	struct WriteThis pooh;

	pooh.readptr = data;
	pooh.sizeleft = strlen(data);

	

	curl = curl_easy_init();
	if (curl) {
		//char* path = Storage_GetAbsolutePathInImagePackage("certs/");
		//curl_easy_setopt(curl, CURLOPT_CAINFO, path);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
		/* First set the URL that is about to receive our POST. */
		struct curl_slist* hs = NULL;
		hs = curl_slist_append(hs, "Content-Type: application/json");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hs);

		curl_easy_setopt(curl, CURLOPT_URL, url);

		/* Now specify we want to POST data */
		curl_easy_setopt(curl, CURLOPT_POST, 1L);

		/* we want to use our own read function */
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);

		/* pointer to pass to our read function */
		curl_easy_setopt(curl, CURLOPT_READDATA, &pooh);

		/* get verbose debug output please */
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

		/*
		  If you use POST to a HTTP 1.1 server, you can send data without knowing
		  the size before starting the POST if you use chunked encoding. You
		  enable this by adding a header like "Transfer-Encoding: chunked" with
		  CURLOPT_HTTPHEADER. With HTTP 1.0 or without chunked transfer, you must
		  specify the size in the request.
		*/
#ifdef USE_CHUNKED
		{
			struct curl_slist* chunk = NULL;

			chunk = curl_slist_append(chunk, "Transfer-Encoding: chunked");
			res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
			/* use curl_slist_free_all() after the *perform() call to free this
			   list again */
		}
#else
		/* Set the expected POST size. If you want to POST large amounts of data,
		   consider CURLOPT_POSTFIELDSIZE_LARGE */
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (curl_off_t)pooh.sizeleft);
#endif

#ifdef DISABLE_EXPECT
		/*
		  Using POST with HTTP 1.1 implies the use of a "Expect: 100-continue"
		  header.  You can disable this header with CURLOPT_HTTPHEADER as usual.
		  NOTE: if you want chunked transfer too, you need to combine these two
		  since you can only set one list of headers with CURLOPT_HTTPHEADER. */

		  /* A less good option would be to enforce HTTP 1.0, but that might also
			 have other implications. */
		{
			struct curl_slist* chunk = NULL;

			chunk = curl_slist_append(chunk, "Expect:");
			res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
			/* use curl_slist_free_all() after the *perform() call to free this
			   list again */
		}
#endif

		/* Perform the request, res will get the return code */
		res = curl_easy_perform(curl);

		/* always cleanup */
		curl_easy_cleanup(curl);
	}
	return 0;
}

struct SafetyValues
{
	GPIO_Value flameRolloutGrill,
			   flameRolloutFront,
			   indusorPressure,
			   blowerHighHeat,
			   flameSensor,
			   button;
};

struct RelayValues
{
	GPIO_Value inducer,
			   blower,
			   ignitor,
			   gasvalve
};

struct TimeStamps
{
	time_t inducerTurnedOn,
		   ignitorTurnedOn
		   
};

int main(void)
{
    // This minimal Azure Sphere app repeatedly toggles GPIO 9, which is the green channel of RGB
    // LED 1 on the MT3620 RDB.
    // Use this app to test that device and SDK installation succeeded that you can build,
    // deploy, and debug an app with Visual Studio, and that you can deploy an app over the air,
    // per the instructions here: https://docs.microsoft.com/azure-sphere/quickstarts/qs-overview
    //
    // It is NOT recommended to use this as a starting point for developing apps; instead use
    // the extensible samples here: https://github.com/Azure/azure-sphere-samples
    Log_Debug(
        "\nVisit https://github.com/Azure/azure-sphere-samples for extensible samples to use as a "
        "starting point for full applications.\n");


	const int MAX_IGNITOR_TIME = 15;
	const int MAX_INDUCER_TIME = 5;
	const GPIO_Value HAS_FLAME = GPIO_Value_Low;
	const GPIO_Value RELAYON = GPIO_Value_Low;
    int fd = GPIO_OpenAsOutput(9, GPIO_OutputMode_PushPull, GPIO_Value_High);
	int fd_btn = GPIO_OpenAsInput(12);
	int fd_pressureSensor = GPIO_OpenAsInput(34);
	int fd_rolloutGrill = GPIO_OpenAsInput(32);
	int fd_rolloutFront = GPIO_OpenAsInput(33);
	int fd_blowerTempHigh = GPIO_OpenAsInput(31);

	int fd_ignitor  = GPIO_OpenAsOutput(42, GPIO_OutputMode_PushPull, GPIO_Value_High);
	int fd_inducer  = GPIO_OpenAsOutput(16, GPIO_OutputMode_PushPull, GPIO_Value_High);
	int fd_blower   = GPIO_OpenAsOutput(43, GPIO_OutputMode_PushPull, GPIO_Value_High);
	int fd_gasvalve = GPIO_OpenAsOutput(17, GPIO_OutputMode_PushPull, GPIO_Value_High);
	
	int fd_flamesensor = GPIO_OpenAsInput(1);
	int dhtpin = 2;
	
	struct SafetyValues oldValues = { GPIO_Value_High, GPIO_Value_High, GPIO_Value_High, GPIO_Value_High, GPIO_Value_High, GPIO_Value_High };
	struct SafetyValues newValues = { GPIO_Value_High, GPIO_Value_High, GPIO_Value_High, GPIO_Value_High, GPIO_Value_High, GPIO_Value_High };

	struct RelayValues relayValues = { GPIO_Value_High, GPIO_Value_High , GPIO_Value_High , GPIO_Value_High };
	struct RelayValues oldRelayValues = { GPIO_Value_High, GPIO_Value_High , GPIO_Value_High , GPIO_Value_High };
	struct TimeStamps timestamps = { 0, 0 };

	time_t dht_timestamp = time(NULL);
	i2cFd = I2CMaster_Open(I2C_ID);
	if (i2cFd < 0)
		Log_Debug("Error opening I2C %s (%d)", strerror(errno), errno);
	I2CMaster_SetBusSpeed(i2cFd, 100000);
	sd1306_init();

    const struct timespec sleepTime = {0, 500};
	
	DHT_SensorData* dht_data = NULL;
	float humidity = 0;
	float temp = 0;

	//Display buffers
	char tempOutput[20] = "Temp = N/A";
	char humidityOutput[20] = "Humidity = N/A";
	char flameOutput[20] = "";
	char statusLine1[22] = "";
	char statusLine2[22] = "";
	char statusLine3[22] = "";


	bool furnaceOn = false;

    while (true) {
		//Read Humidity and Temp Data
		if (difftime(time(NULL), dht_timestamp) > 2)
		{
			dht_data = DHT_ReadData(dhtpin);
			dht_timestamp = time(NULL);

			if (dht_data != NULL)
			{
				humidity = dht_data->Humidity;
				temp = dht_data->TemperatureFahrenheit;
			}
		}
		GPIO_GetValue(fd_flamesensor, &(newValues.flameSensor));
		GPIO_GetValue(fd_pressureSensor, &(newValues.indusorPressure));
		GPIO_GetValue(fd_blowerTempHigh, &(newValues.blowerHighHeat));
		GPIO_GetValue(fd_rolloutFront, &(newValues.flameRolloutGrill));
		GPIO_GetValue(fd_rolloutFront, &(newValues.flameRolloutFront));
		GPIO_GetValue(fd_btn, &(newValues.button));

		//Do tests here
		if (newValues.button == GPIO_Value_Low && oldValues.button == GPIO_Value_High)
		{
			furnaceOn = !furnaceOn;
		}

		if (furnaceOn)
		{
			//If furnce was just turned on
			//or the inducer hasn't been on for more than 5 seconds yet
			//or the inducer pressure is detected
			//Then keep the inducer on
			if (timestamps.inducerTurnedOn == 0 || difftime(time(NULL), timestamps.inducerTurnedOn) < MAX_INDUCER_TIME || newValues.indusorPressure == GPIO_Value_High)
			{
				relayValues.inducer = GPIO_Value_Low;
				if (timestamps.inducerTurnedOn == 0)
				{
					timestamps.inducerTurnedOn = time(NULL);
				}
			}
			else
			{
				relayValues.inducer = GPIO_Value_High;
				furnaceOn = false;
			}

			//if the inducer relay is on
			//and inducer pressure is detected
			//and the ignitor has not been on for more than 10 seconds
			//then keep the ignitor on
			if (relayValues.inducer == GPIO_Value_Low && newValues.indusorPressure == GPIO_Value_High && (timestamps.ignitorTurnedOn == 0 || difftime(time(NULL), timestamps.ignitorTurnedOn) < MAX_IGNITOR_TIME))
			{
				if (timestamps.ignitorTurnedOn == 0)
				{
					timestamps.ignitorTurnedOn = time(NULL);
				}
				relayValues.ignitor = GPIO_Value_Low;
			}
			else
			{
				relayValues.ignitor = GPIO_Value_High;
			}

			if (newValues.indusorPressure == GPIO_Value_High && ((relayValues.ignitor == GPIO_Value_Low && difftime(time(NULL), timestamps.ignitorTurnedOn) > MAX_IGNITOR_TIME / 2) || newValues.flameSensor == HAS_FLAME))
			{
				relayValues.gasvalve = GPIO_Value_Low;
			}
			else
			{
				relayValues.gasvalve = GPIO_Value_High;
				if (oldRelayValues.gasvalve == RELAYON)
					furnaceOn = false;
			}


			if (newValues.indusorPressure == GPIO_Value_High && newValues.flameSensor == HAS_FLAME)
			{
				relayValues.blower = GPIO_Value_Low;
			}
			else
				relayValues.blower = GPIO_Value_High;
			
			//If the gas valve is open
			//and no flame is detected
			//and the ignitor has been on for more than 10 seconds
			//then shut it down
			if (oldRelayValues.gasvalve == RELAYON && oldRelayValues.ignitor != RELAYON && newValues.flameSensor != HAS_FLAME)
				furnaceOn = false;

			if (oldValues.flameSensor == HAS_FLAME && newValues.flameSensor != HAS_FLAME)
				furnaceOn = false;


		}

		if (!furnaceOn) // shut everything down
		{
			relayValues.gasvalve = GPIO_Value_High;
			relayValues.ignitor = GPIO_Value_High;
			relayValues.blower = GPIO_Value_High;
			relayValues.inducer = GPIO_Value_High;

			timestamps.ignitorTurnedOn = 0;
			timestamps.inducerTurnedOn = 0;
		}
		

		GPIO_SetValue(fd_gasvalve, relayValues.gasvalve);
		GPIO_SetValue(fd_ignitor, relayValues.ignitor);
		GPIO_SetValue(fd_blower, relayValues.blower);
		GPIO_SetValue(fd_inducer, relayValues.inducer);

		//Display Results here

		sprintf(&tempOutput, "Temp = %.2f deg F", temp);
		sprintf(&humidityOutput, "Humidity = %.2f", humidity);
		sprintf(&flameOutput, "Flame = %s", newValues.flameSensor == HAS_FLAME ? "yes" : "no");

		sprintf(&statusLine3, "Furnace is %s", furnaceOn ? "ON" : "OFF");

		if (relayValues.inducer == RELAYON && newValues.indusorPressure == GPIO_Value_Low)
		{
			sprintf(&statusLine1, "Building pressure...");
			sprintf(&statusLine2, "Time Left: %d", (int)(MAX_INDUCER_TIME - difftime(time(NULL), timestamps.inducerTurnedOn)));
		}
		else if (relayValues.ignitor == RELAYON && relayValues.gasvalve != RELAYON)
		{
			sprintf(&statusLine1, "Warming Up");
			sprintf(&statusLine2, "Time Left: %d", (int)(MAX_IGNITOR_TIME/2 - difftime(time(NULL), timestamps.ignitorTurnedOn)));
		}
		else if (relayValues.ignitor == RELAYON && relayValues.gasvalve == RELAYON && newValues.flameSensor != HAS_FLAME)
		{
			sprintf(&statusLine1, "Starting fire!!!");
			sprintf(&statusLine2, "Time Left: %d", (int)(MAX_IGNITOR_TIME - difftime(time(NULL), timestamps.ignitorTurnedOn)));
		}
		else if (newValues.indusorPressure == GPIO_Value_High && newValues.flameSensor == HAS_FLAME && relayValues.blower == RELAYON && relayValues.gasvalve == RELAYON)
		{
			sprintf(&statusLine1, "Born 2 Burn");
			sprintf(&statusLine1, "");
		}
		else {
			sprintf(&statusLine1, "");
			sprintf(&statusLine2, "");
		}
		
		
		//if (hasFlame != oldHasFlame)
		//{
		//	GPIO_SetValue(fd_gasvalve, hasFlame);
		//	nanosleep(&relayDelay, NULL);
		//	GPIO_SetValue(fd_blower, hasFlame);
		//	nanosleep(&relayDelay, NULL);
		//	GPIO_SetValue(fd_ignitor, hasFlame);
		//	nanosleep(&relayDelay, NULL);
		//	GPIO_SetValue(fd_inducer, hasFlame);

		//	oldHasFlame = hasFlame;
		//	char json[50];
		//	
		//	sprintf(&json, "{\"Temp\":%.2f,\"Humidity\":%.2f,\"FlameOn\":%s}", temp, humidity, hasFlame == GPIO_Value_High ? "true" : "false");
		//	Log_Debug(json);
		//	//post("https://sphere-data-service.azurewebsites.net/api/SphereLog", json);
		//}


		clear_oled_buffer();
		sd1306_draw_string(0, 0, &tempOutput, 1, 1);
		sd1306_draw_string(0, 8, &humidityOutput, 1, 1);
		sd1306_draw_string(0, 16, &flameOutput, 1, 1);

		sd1306_draw_string(0, 24, &statusLine1, 1, 1);
		sd1306_draw_string(0, 32, &statusLine2, 1, 1);
		sd1306_draw_string(0, 40, &statusLine3, 1, 1);
		sd1306_refresh();

		oldValues.blowerHighHeat = newValues.blowerHighHeat;
		oldValues.flameRolloutFront = newValues.flameRolloutFront;
		oldValues.flameRolloutGrill = newValues.flameRolloutGrill;
		oldValues.indusorPressure = newValues.indusorPressure;
		oldValues.flameSensor = newValues.flameSensor;
		oldValues.button = newValues.button;

		oldRelayValues.blower = relayValues.blower;
		oldRelayValues.gasvalve = relayValues.gasvalve;
		oldRelayValues.ignitor = relayValues.ignitor;
		oldRelayValues.inducer = relayValues.inducer;

		nanosleep(&sleepTime, NULL);
		
    }
}


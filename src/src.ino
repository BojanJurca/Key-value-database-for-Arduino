#include <WiFi.h>

#include <LittleFS.h> // or #include <FFat.h> or #include <SD.h>
#define fileSystem LittleFS // or FFat or SD


/* place all the internal memory structures into PSRAM if the bord has one
    #define VECTOR_QUEUE_MEMORY_TYPE  PSRAM_MEM
    #define MAP_MEMORY_TYPE           PSRAM_MEM
    bool psramInitialized = psramInit ();
*/
#include "keyValuedatabase.hpp"

keyValueDatabase<String, String> settings;  // a key-value database instance where keys are Strings and values are Strings


WiFiServer webServer (80);
keyValueDatabase<String, unsigned int> hitCount;  // a key-value database instance where keys are Strings and values are unsigned ints


void setup () {
    Serial.begin (115200);
    while (!Serial) 
        delay (10);
    delay (1000);


    fileSystem.begin ();
    // if (fileSystem.format ()) Serial.println ("LittleFs formatted"); else Serial.println ("LittleFs formatting failed");


    // ----- QUICK START EXAMPLE -----

    settings.Open ("/settings.db");
    // settings.Truncate ();

    #define defaultSSID "*****"
    #define defaultPassword "*****"

    // read values that belong to given keys from database
    String SSID = settings ["SSID"];
    String password = settings ["password"];

    if (SSID == "") {
        Serial.println ("Initializing settings database with default WiFi credentials");

        // write key-value pairs to database
        settings ["SSID"] = SSID = defaultSSID;
        settings ["password"] = password = defaultPassword;
    } else {
        Serial.println ("WiFi credentials read from settings database");
    }

    // check for error or success
    signed char e = settings.errorFlags ();
    if (e) { 
        // check error details
        if (e & err_bad_alloc)      Serial.println ("settings err_bad_alloc");
        if (e & err_not_found)      Serial.println ("settings err_not_found");
        if (e & err_not_unique)     Serial.println ("settings err_not_unique");
        if (e & err_data_changed)   Serial.println ("settings err_data_changed");
        if (e & err_file_io)        Serial.println ("settings err_file_io");
        if (e & err_cant_do_it_now) Serial.println ("settings err_cant_do_it_now");

        settings.clearErrorFlags ();
    } else {
        Serial.println ("settings OK");
    }

    // list the whole database
    for (auto p: settings) {
        String value;
        e = settings.FindValue (p.key, &value, p.blockOffset);
        if (!e) { // OK
            Serial.print (p.key); Serial.print (" - "); Serial.println (value);
        } else {
            // error ...
            Serial.println ("Error " + String (e) + " fetching a value for " + p.key);
        }
    }

    // test
    WiFi.begin (SSID, password);
    while (WiFi.localIP ().toString () == "0.0.0.0") { // wait until we get IP from the router
        delay (1000);
        Serial.printf ("   .\n");
    }

    Serial.printf ("Got IP: %s\n", (char *) WiFi.localIP ().toString ().c_str ());


    // ----- A LITTLE LONGER START EXAMPLE -----

    webServer.begin ();

    hitCount.Open ("/hitCount.db");
    // hitCount.Truncate ();


    // Insert: there are 2 possible ways to insert a new record.
    //         1. the simple one is using [] operator like
    //              ["GET / HTTP/1.1"] = 0;
    //
    //         2. the second one is using Insert function: 
                    e = hitCount.Insert ("GET / HTTP/1.1", 0);
                    if (e) // != OK
                        Serial.println ("hitCount Insert failed with error " + String (e) + " if the error is err_not_unique (-120) it means that the key already exists.");

    /* Delete: can be used in a similar way:
                    e = hitCount.Delete ("GET / HTTP/1.1");
                    if (e) // != OK
                        Serial.println ("hitCount Delete failed with error " + String (e));
    */

    // Find: there are 2 possible ways to find a value in a database.
    //       1. the simple one is using [] operator like
    //          unsigned int rootCount = hitCount ["GET / HTTP/1.1"];
    //
    //       2. the second is using FindValue function
                    unsigned int rootCount;
                    e = hitCount.FindValue ("GET / HTTP/1.1", &rootCount);
                    switch (e) {
                        case err_ok:        Serial.printf ("GET / HTTP/1.1 has been accessed %u times so far\n", rootCount);
                                            break;
                        case err_not_found: Serial.printf ("GET / HTTP/1.1 not found in hitCount\n");
                                            break;
                        default:            Serial.printf ("FindValue resulted in error %i\n", e);
                                            break;
                    }

    // Update the whole database - not that it would make much sense right here ... please note that iterating is thread-safe since the key-value database is locked meanwhile
    for (auto p: hitCount) {
        /* set the counters to specific value:
        e = hitCount.Update (p.key, (unsigned int) 0, &p.blockOffset);
        if (e) // error
            Serial.println ("Error " + String (e) + " updating a value for " + p.key);
        */
        // calculate new counter value from existing one with the help of lambda callback function
        e = hitCount.Update (p.key, [] (unsigned int& value) { value ++; }, &p.blockOffset);
          if (e) // error
              Serial.println ("Error " + String (e) + " updating a value for " + p.key);
    }

}

unsigned int hits;

void loop () {
    WiFiClient webClient = webServer.accept ();
    if (webClient) {
        String httpRequest = "";
        while (webClient.connected ()) {
            if (webClient.available ()) {
                httpRequest += (char) webClient.read ();
                if (httpRequest.endsWith ("\r\n\r\n")) { // the whole HTTP request has been red
                    Serial.println ("HTTP request from " + webClient.remoteIP ().toString () + ":\r\n" + httpRequest); 

                    // extract URL from httpRequest
                    int i = httpRequest.indexOf (" ");
                    if (i < 0)
                        break;
                    int j = httpRequest.indexOf (" ", i + 1);
                    if (j < 0)
                        break;
                    String URL = httpRequest.substring (i + 1, j);

                    // Update/Upsert: there are 4 possible ways to update/upsert a value in a database.
                    //       1. the straightforward one is using expression with [] operators like
                    //
                    //            hitCount [httpRequest] = hits = hitCount [httpRequest] + 1;
                    //
                    //       Although this works in a single-threaded environment, it is not thread-safe. 
                    //
                    //       2. to use expressions in a multi-threaded environment the tatabase should be locked during expression calculation:
                    //
                    //            hitCount.Lock ();
                    //            hitCount [httpRequest] = hits = hitCount [httpRequest] + 1;
                    //            hitCount.Unlock ();
                    //
                    //      3. the third option is using ++ operator together with [] operator, which is thread-safe:
                    //
                                  hits = ++ hitCount [URL];
                    //
                    //      The following operators are all thread safe: prefix and postfix ++ and --, +=, -=, *=, /=
                    //
                    //      4. the fourth option is using (lambda) callback function, which is the fastest and also thread safe.
                    //
                    //            signed char e = hitCount.Upsert (httpRequest, [] (unsigned int& value) { hits = ++ value; } ); 
                    //            if (e) // error
                    //                ...

                    String httpReplyBody = "<HTML><BODY>The page " + URL + " has been accessed " + String (hits) + " times</BODY></HTML>"; // always send a similar reply
                    // Serial.println ("Body of HTTP reply:\r\n" + httpReplyBody);
                    webClient.print ("HTTP/1.1 200 OK\r\n"
                                    "Content-type: text/html\r\n"
                                    "Connection: close\r\n"
                                    "Content-Length: " + String (httpReplyBody.length ()) + "\r\n"
                                    "\r\n" +
                                    httpReplyBody);
                    webClient.stop ();
                    return;
                }
            } else {
                delay (10);
            }
        }
    }
}

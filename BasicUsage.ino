#include <WiFi.h>

#define FILE_SYSTEM   FILE_SYSTEM_LITTLEFS // or FILE_SYSTEM FILE_SYSTEM_FAT 
#include "fileSystem.hpp"

#include "persistentKeyValuePairs.hpp"

persistentKeyValuePairs<int, String> pkvpA;


void setup () {
    Serial.begin (115200);
    delay (3000);


    // fileSystem.formatLittleFs (); Serial.printf ("\nFormatting LittleFS file system ...\n\n"); // format flash disk to reset everithing and start from the scratch
    fileSystem.mountLittleFs (true); // or fileSystem.mountFAT (true);
    // create directory for data files
    if (!fileSystem.isDirectory ("/var/persistentKeyValuePairs")) { fileSystem.makeDirectory ("/var"); fileSystem.makeDirectory ("/var/persistentKeyValuePairs"); }
    // fileSystem.deleteFile ("/var/persistentKeyValuePairs/A.kvp");

    signed char e;

    // index values in the data file (load keys to balanced binary search tree)
    e = pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");
    if (!e) //  OK 
        Serial.printf ("pkvpA initially loaded %i key-value pairs\n", pkvpA.size ());
    else
        Serial.printf ("pkvpA failed to load data, check errorFalgs () for details\n");


    // truncate (delete) all key-value pairs in case there are some already in pkvpA
    pkvpA.Truncate ();


    // Example of inserting new key-value pairs
    e = pkvpA.Insert (7, "seven"); 
    if (e) // != OK
        Serial.printf ("pkvpA Insert failed, check errorFalgs () for details\n");
    e = pkvpA.Insert (8, "eigth"); 
    if (e) // != OK
        Serial.printf ("pkvpA Insert failed, check errorFalgs () for details\n");
    e = pkvpA.Insert (9, "nine"); 
    if (e) // != OK
        Serial.printf ("pkvpA Insert failed, check errorFalgs () for details\n");


    // Examples of searching for a key
    uint32_t blockOffset;
    e = pkvpA.FindBlockOffset (8, blockOffset);
    switch (e) {
        case OK:        Serial.printf ("pkvpA key 8 FOUND\n"); 
                        break;
        case NOT_FOUND: Serial.printf ("pkvpA key NOT_FOUND\n"); 
                        break;
        default:        Serial.printf ("pkvpA FindBlockOffset error: %i\n", e); 
                        break;
    }

    // Examples of searching for a value that belongs to a given key
    String value;
    e = pkvpA.FindValue (8, &value); 
    switch (e) {
        case OK:        Serial.printf ("pkvpA value for a key 8 FOUND: %s\n", (char *) value.c_str ()); 
                        break;
        case NOT_FOUND: Serial.printf ("pkvpA key NOT_FOUND\n"); 
                        break;
        default:        Serial.printf ("pkvpA FindValue error: %i\n", e); 
                        break;
    }    


    // Example of deleting a key-value pair
    e = pkvpA.Delete (7); 
    if (e) // != OK
        Serial.printf ("pkvpA Delete failed, check errorFalgs () for details\n");


    // Examples of updating the value for a given key

    // update (1) the value (most simple version of calling update)
    e = pkvpA.Update (8, "eight"); 
    if (e) // != OK
        Serial.printf ("pkvpA Update failed, check errorFalgs () for details\n");

    // update (2) the value with calculation (update with locking to prevent other tasks changind the data while the calculation is not finished)
    pkvpA.Lock ();
        String oldValue;
        e = pkvpA.FindValue (8, &oldValue); 
        if (e == OK)
            e = pkvpA.Update (8, oldValue + "teen"); 
    pkvpA.Unlock ();
    if (e) // != OK
        Serial.printf ("pkvpA Update failed, check errorFalgs () for details\n");

    // update (3) the value with calculation using lambda callback function, the locking is already integrated so the calculation can be performed without problems
    // this mechanism is usefull for example for counters (increasing the value), etc)
    e = pkvpA.Update (9, [] (String& value) { value.toUpperCase (); } ); 
    if (e) // != OK
        Serial.printf ("pkvpA Update failed, check errorFalgs () for details\n");


    // Example of iterating through all key-value pairs
    for (auto p: pkvpA) {
        // keys are always kept in memory and are obtained fast
        Serial.print (p->key); Serial.print (", "); Serial.print (p->blockOffset); Serial.print (" -> "); 
        
        // values are read from disk, obtaining a value may be much slower
        String value;
        e = pkvpA.FindValue (p->key, &value, p->blockOffset); // blockOffset is optional but since we already have it we can speed up the search a bit by providing it
        if (e) // != OK 
            Serial.printf ("FindValue error, check errorFalgs () for details\n");
        else
            Serial.println (value);
    }


    // Example of updating the values (with calculation) during iteration
    // Please note that persistent key value pairs are already locked throughtout the iterator, so additional locking is not required.
    for (auto p: pkvpA) {
        e = pkvpA.Update (p->key, [] (String& value) { value = "»" + value + "«"; }, &(p->blockOffset)); // since block offset is already known it will speed up Update operation if we provide this information
        if (e) 
            Serial.printf ("Update error, check errorFalgs () for details\n");
    }


    // Detecting errors that occured in persistent key-value pairs operations
    for (auto p: pkvpA) {
        String value;
        e = pkvpA.FindValue (p->key, &value, p->blockOffset);
        if (!e) { // OK
            Serial.print (p->key); Serial.print (" - "); Serial.println (value);     
        } else {
            Serial.printf ("FindValue error while fetching a value from disk: ");
            switch (e) {
                  case BAD_ALLOC:       Serial.printf ("BAD_ALLOC\n"); break;
                  case NOT_FOUND:       Serial.printf ("NOT_FOUND\n"); break;
                  case NOT_UNIQUE:      Serial.printf ("NOT_UNIQUE\n"); break;
                  case DATA_CHANGED:    Serial.printf ("DATA_CHANGED\n"); break;
                  case FILE_IO_ERROR:   Serial.printf ("FILE_IO_ERROR\n"); break;
                  case CANT_DO_IT_NOW:  Serial.printf ("CANT_DO_IT_NOW\n"); break;
              }
        }
    }


    // checking errors only once for multiple operations
    pkvpA.clearErrorFlags ();
    for (int i = 1000; i < 1100; i++)
        pkvpA.Insert (i, String (i));

    e = pkvpA.errorFlags ();
    if (e) { // != OK
        Serial.printf ("100 inserts error:\n");
        if (e & BAD_ALLOC)      Serial.println ("BAD_ALLOC");
        if (e & NOT_FOUND)      Serial.println ("NOT_FOUND");
        if (e & NOT_UNIQUE)     Serial.println ("NOT_UNIQUE");
        if (e & DATA_CHANGED)   Serial.println ("DATA_CHANGED");
        if (e & FILE_IO_ERROR)  Serial.println ("FILE_IO_ERROR");
        if (e & CANT_DO_IT_NOW) Serial.println ("CANT_DO_IT_NOW");
    }


    // find first (last) keys
    auto firstElement = first_element (pkvpA);
    if (firstElement) // check if first element is found (if pkvpA is not empty)
        Serial.printf ("first element (min key) of pkvpA = %i\n", (*firstElement)->key);

    auto lastElement = last_element (pkvpA);
    if (lastElement) // check if last element is found (if pkvpA is not empty)
        Serial.printf ("last element (max key) of pkvpA = %i\n", (*lastElement)->key);


                // capacity and speed test
                persistentKeyValuePairs<unsigned long, String> pkvpB;
                pkvpB.loadData ("/var/persistentKeyValuePairs/B.kvp");
                if (pkvpB.errorFlags ()) // != OK 
                    Serial.printf ("pkvpB failed to load data: %i, all the data may not be indexed\n", pkvpB.errorFlags ());
                else
                    Serial.printf ("pkvpB initially loaded %i key-value pairs\n", pkvpB.size ());
                pkvpB.Truncate ();
                pkvpB.clearErrorFlags ();
                unsigned long l;
                unsigned long startMillis = millis ();
                for (l = 1; l <= 100000; l++) {
                    if (pkvpB.Insert ( l, "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum." ))
                         break;
                    if (l % 100 == 0)
                        Serial.printf ("%lu ... %lu Bytes ...\n", pkvpB.size (), pkvpB.dataFileSize ());
                }
                e = pkvpB.errorFlags ();
                if (e) { // != OK
                    if (e & BAD_ALLOC)      Serial.println ("BAD_ALLOC");
                    if (e & NOT_FOUND)      Serial.println ("NOT_FOUND");
                    if (e & NOT_UNIQUE)     Serial.println ("NOT_UNIQUE");
                    if (e & DATA_CHANGED)   Serial.println ("DATA_CHANGED");
                    if (e & FILE_IO_ERROR)  Serial.println ("FILE_IO_ERROR");
                    if (e & CANT_DO_IT_NOW) Serial.println ("CANT_DO_IT_NOW");
                }
                pkvpB.Truncate ();
                unsigned long endMillis = millis ();

                Serial.printf ("Free heap: %lu, free PSRAM: %lu\n", ESP.getFreeHeap (), ESP.getFreePsram ());
                Serial.printf ("Maximum number of PersistentKeyValuePairs<unsigned long, (long String text) )> in the memory or data file (whichever error occured) is %lu\n", l); 
                // ESP32-S2: 63951 for short string (running out of memory) or 2600 for long string (running out of disk)
                Serial.printf ("Average Insert time = %lu ms\n", (endMillis - startMillis) / l);
                // ESP32-S2: 64 ms
                Serial.printf ("Free heap: %lu, free PSRAM: %lu\n", ESP.getFreeHeap (), ESP.getFreePsram ());
}

void loop () {

}

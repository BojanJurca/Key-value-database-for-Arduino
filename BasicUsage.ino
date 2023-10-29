#include <WiFi.h>

#define FILE_SYSTEM FILE_SYSTEM_FAT // or FILE_SYSTEM_LITTLEFS
#include "fileSystem.hpp"

#include "persistentKeyValuePairs.h"

persistentKeyValuePairs<int, String> pkvpA;


void setup () {
    Serial.begin (115200);

    // fileSystem.formatFAT (); Serial.printf ("\nFormatting FAT file system ...\n\n"); // format flash disk to reset everithing and start from the scratch
    fileSystem.mountFAT (true); // or fileSystem.mountLittleFs (true); // or 

    // create directory for data files
    if (!fileSystem.isDirectory ("/var/persistentKeyValuePairs")) { fileSystem.makeDirectory ("/var"); fileSystem.makeDirectory ("/var/persistentKeyValuePairs"); }
    // fileSystem.deleteFile ("/var/persistentKeyValuePairs/A.kvp");

    // index values in the data file (load keys to balanced binary search tree)
    pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");
    if (pkvpA.lastErrorCode != pkvpA.OK) 
        Serial.printf ("pkvpA failed to load data: %s, all the data may not be indexed\n", pkvpA.errorCodeText (pkvpA.lastErrorCode));
    else
        Serial.printf ("pkvpA initially loaded %i key-value pairs\n", pkvpA.size ());

    persistentKeyValuePairs<int, String>::errorCode e;


    // insert a key-value pairs
    e = pkvpA.Insert (7, "seven"); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Insert failed: %s\n", pkvpA.errorCodeText (e));
    e = pkvpA.Insert (8, "eigth"); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Insert failed: %s\n", pkvpA.errorCodeText (e));
    e = pkvpA.Insert (9, "nine"); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Insert failed: %s\n", pkvpA.errorCodeText (e));


    // find a key (check if it exists). You also get its position in the data file at the same time (just in case you need it).
    uint32_t blockOffset;
    e = pkvpA.FindBlockOffset (8, blockOffset);
    switch (e) {
        case pkvpA.OK:        Serial.printf ("pkvpA key 8 FOUND\n"); break;
        case pkvpA.NOT_FOUND: Serial.printf ("pkvpA key NOT_FOUND\n"); break;
        default:              Serial.printf ("pkvpA FindBlockOffset error: %s\n", pkvpA.errorCodeText (e)); break;
    }

    // find the value for a given key
    String value;
    e = pkvpA.FindValue (8, &value); 
    switch (e) {
        case pkvpA.OK:        Serial.printf ("pkvpA value for a key 8 FOUND: %s\n", (char *) value.c_str ()); break;
        case pkvpA.NOT_FOUND: Serial.printf ("pkvpA key NOT_FOUND\n"); break;
        default:              Serial.printf ("pkvpA FindValue error: %s\n", pkvpA.errorCodeText (e)); break;
    }    


    // delete a key-value pair
    e = pkvpA.Delete (7); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Delete failed: %s\n", pkvpA.errorCodeText (e));


    // update (1) the value (most simple version of calling update)
    e = pkvpA.Update (8, "eight"); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Update failed: %s\n", pkvpA.errorCodeText (e));

    // update (2) the value with calculation (update with locking to prevent other tasks changind the data while the calculation is not finished)
    pkvpA.Lock ();
        String oldValue;
        e = pkvpA.FindValue (8, &oldValue); 
        if (e == pkvpA.OK)
            e = pkvpA.Update (8, oldValue + "teen"); 
    pkvpA.Unlock ();
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Update failed: %s\n", pkvpA.errorCodeText (e));

    // update (3) the value with calculation using lambda callback function
    // (the locking is already integrated so the calculation can be performed without problems - this mechanism is usefull for example for counters (increasing the value), etc)
    e = pkvpA.Update (9, [] (String& value) { value.toUpperCase (); } ); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Update failed: %s\n", pkvpA.errorCodeText (e));


    // Iterate through (list) all key-value pairs
    for (auto p: pkvpA) {
        // keys are always kept in memory and are obtained fast
        Serial.print (p->key); Serial.print (", "); Serial.print (p->blockOffset); Serial.print (" -> "); 
        
        // values are read from disk, obtaining a value may be much slower
        String value;
        e = pkvpA.FindValue (p->key, &value, p->blockOffset);
        if (e == pkvpA.OK) 
            Serial.println (value);
        else
            Serial.printf ("Error: %s\n", pkvpA.errorCodeText (e));
    }


    // update the values (with calculation) during iteration. Please note that persistent key value pairs are already being locked throughtout the iteration, so additional locking is not required.
    for (auto p: pkvpA) {
        String value;
        e = pkvpA.FindValue (p->key, &value, p->blockOffset);
        if (e == pkvpA.OK) 
            pkvpA.Update (p->key, "(" + value + ")", &(p->blockOffset)); // since block offset is already known it will speed up Update operation if we provide this information
        else
            Serial.printf ("Error: %s\n", pkvpA.errorCodeText (e));
    }


    // checking errors only once for multiple operations
    pkvpA.clearLastErrorCode ();
    for (int i = 1000; i < 1100; i++)
        pkvpA.Insert (i, String (i));
    if (pkvpA.lastErrorCode != pkvpA.OK)
        Serial.printf ("100 inserts error: %s\n", pkvpA.errorCodeText (pkvpA.lastErrorCode));


    // truncate (delete) all key-value pairs
    pkvpA.Truncate ();

}

void loop () {

}

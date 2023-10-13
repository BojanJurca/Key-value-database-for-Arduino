#include <WiFi.h>

#define FILE_SYSTEM    FILE_SYSTEM_FAT 
#include "fileSystem.hpp"


#include "persistentKeyValuePairs.h"

persistentKeyValuePairs<int, String> pkvpA;


void setup () {
    Serial.begin (115200);

    // fileSystem.formatFAT (); Serial.printf ("\nFormatting FAT file system ...\n\n"); // format flash disk to reset everithing and start from the scratch
    fileSystem.mountFAT (true);    

    // create directory for data files
    if (!fileSystem.isDirectory ("/var/persistentKeyValuePairs")) { fileSystem.makeDirectory ("/var"); fileSystem.makeDirectory ("/var/persistentKeyValuePairs"); }

    // index values in the data file (load keys into balanced binary search tree)
    pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");
    if (pkvpA.lastErrorCode != pkvpA.OK) 
        Serial.printf ("pkvpA failed to load data: %s, all the data may not be indexed\n", pkvpA.errorCodeText (pkvpA.lastErrorCode));
    else
        Serial.printf ("pkvpA initially loaded %i key-value pairs\n", pkvpA.size ());

    persistentKeyValuePairs<int, String>::errorCode e;

    // insert key-value pairs
    e = pkvpA.Insert (7, "seven"); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Insert failed: %s\n", pkvpA.errorCodeText (e));
    e = pkvpA.Insert (8, "eigth"); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Insert failed: %s\n", pkvpA.errorCodeText (e));
    e = pkvpA.Insert (9, "nine"); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Insert failed: %s\n", pkvpA.errorCodeText (e));

    // delete a key-value pair
    e = pkvpA.Delete (7); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Delete failed: %s\n", pkvpA.errorCodeText (e));

    // update a key-value pair
    e = pkvpA.Update (8, "eight"); 
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

    // checking errors once for multiple operations
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

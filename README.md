# Local (built-in flash disk) key-value database for Arduino (ESP32 boards)


Persistent key-value pairs library with structured data storage. 


### Example of opening the data file and loading stored data

```C++
#include <WiFi.h>

#define FILE_SYSTEM FILE_SYSTEM_LITTLEFS // or FILE_SYSTEM_FAT, ...
#include "fileSystem.hpp"

#include "persistentKeyValuePairs.hpp"
persistentKeyValuePairs<int, String> pkvpA;

void setup () {
    Serial.begin (115200);

    // fileSystem.formatLittleFs ();
    fileSystem.mountLittleFs (true); // or fileSystem.mountFAT (true);

    // index values in the data file (load keys to balanced binary search tree)
    pkvpA.loadData ("/A.kvp");
```


### Example of inserting new key-value pair

```C++
persistentKeyValuePairs<int, String>::errorCode e;

e = pkvpA.Insert (7, "seven"); 
if (e) // != OK
    Serial.printf ("pkvpA Insert failed, check errorFalgs () for details\n");
```


### Examples of searching for a key

```C++
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
```


### Examples of searching for a value that belongs to a given key

```C++
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
```


### Examples of updating the value for a given key

```C++
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
```


### Example of deleting a key-value pair

```C++
e = pkvpA.Delete (7); 
if (e) // != OK
    Serial.printf ("pkvpA Delete failed, check errorFalgs () for details\n");
```


### Example of truncating key-value pair database (delete all the key-value pairs)

```C++
pkvpA.Truncate ();
```


### Example of iterating through all key-value pairs

```C++
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
```


### Example of updating the values (with calculation) during iteration

```C++
// Please note that persistent key value pairs are already locked throughtout the iterator, so additional locking is not required.
for (auto p: pkvpA) {
    e = pkvpA.Update (p->key, [] (String& value) { value = "»" + value + "«"; }, &(p->blockOffset)); // since block offset is already known it will speed up Update operation if we provide this information
    if (e) 
        Serial.printf ("Update error, check errorFalgs () for details\n");
}
```


### Detecting errors that occured in persistent key-value pairs operations

```C++
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
```


### Finding the first and the last persistent key-value pairs (min and max keys)

```C++
auto firstElement = first_element (pkvpA);
if (firstElement) // check if first element is found (if pkvpA is not empty)
    Serial.printf ("first element (min key) of pkvpA = %i\n", (*firstElement)->key);

auto lastElement = last_element (pkvpA);
if (lastElement) // check if last element is found (if pkvpA is not empty)
    Serial.printf ("last element (max key) of pkvpA = %i\n", (*lastElement)->key);
```


### Checking if an error has occurred only once after many persistent key-value pairs operations

```C++
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
```


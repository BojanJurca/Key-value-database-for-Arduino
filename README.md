# Simple Arduino key-value database (ESP32 boards with flash disk)


Persistent key-value pairs library that offers a kind of structured data storage. It can be used as a small and simple (local to ESP32) database. 


## Basic operations (see BasicUsage.ino)

```C++
#include <WiFi.h>

#define FILE_SYSTEM FILE_SYSTEM_FAT // or FILE_SYSTEM_LITTLEFS
#include "fileSystem.hpp"

#include "persistentKeyValuePairs.h"
persistentKeyValuePairs<int, String> pkvpA;

void setup () {
    Serial.begin (115200);

    // fileSystem.formatFAT ();
    fileSystem.mountFAT (true); // or fileSystem.mountLittleFs (true);

    // index values in the data file (load keys to balanced binary search tree)
    pkvpA.loadData ("/A.kvp");
```

### Insert (new key-value pair)

```C++
    persistentKeyValuePairs<int, String>::errorCode e;

    e = pkvpA.Insert (7, "seven"); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Insert failed: %s\n", pkvpA.errorCodeText (e));
```

### Find (value if a key is known)

```C++
        String value;
        e = pkvpA.FindValue (p->key, &value, p->blockOffset);
        if (e == pkvpA.OK) 
            Serial.println (value);
        else
            Serial.printf ("Error: %s\n", pkvpA.errorCodeText (e));
```

### Update (value for a given key)

```C++
    e = pkvpA.Update (8, "eight"); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Update failed: %s\n", pkvpA.errorCodeText (e));
```

### Delete (key-value pair)

```C++
    e = pkvpA.Delete (7); 
    if (e != pkvpA.OK)
        Serial.printf ("pkvpA Delete failed: %s\n", pkvpA.errorCodeText (e));
```

### Truncate (delete all the key-value pairs)

```C++
    pkvpA.Truncate ();
```

### Iterate (through all the key-value pairs)

```C++
    for (auto p: pkvpA) {
        // keys are always kept in memory and are obtained fast
        Serial.print (p.key); Serial.print (", "); Serial.print (p.blockOffset); Serial.print (" -> "); 
        
        // values are read from disk, obtaining a value may be much slower
        String value;
        e = pkvpA.FindValue (p.key, &value, p.blockOffset);
        if (e == pkvpA.OK) 
            Serial.println (value);
        else
            Serial.printf ("Error: %s\n", pkvpA.errorCodeText (e));
    }
```


## Key value database for Arduino reference manual

persistentKeyValuePairs is a C++ class template with two parameters:

- keyType: C++ type, including Arduino String
- valueType: C++ type, including Arduino String

Example of usage:

```C++
    #include "persistentKeyValuePairs.h"
    persistentKeyValuePairs<int, String> pkvpA;
```

### Error handling

- enum errorCode (class member): most of the functions return an error code

```C++
    enum errorCode {OK = 0, // not all error codes are needed here but they are shared among keyValuePairs and vectors as well 
                    NOT_FOUND = -1, // key is not found
                    BAD_ALLOC = -2, // out of memory
                    OUT_OF_RANGE = -3, // invalid index
                    NOT_UNIQUE = -4, // the key is not unique
                    DATA_CHANGED = -5, // unexpected data value found
                    FILE_IO_ERROR = -6, // file operation error
                    NOT_WHILE_ITERATING = -7, // operation can not be performed while iterating
                    DATA_ALREADY_LOADED = -8 // can't load the data if it is already loaded 
```

- char *errorCodeText (int e) (class member function): returns a textual representation of each of the error codes
- errorCode lastErrorCode (class member variable): contains the last error code set by different functions
- void clearLastErrorCode () (class member function): clears lastErrorCode (sets it to OK)


### errorCode loadData (const char *dataFileName) 

Class member function loads the keys and their locations in the data file (dataFileOffsets) from dataFileName to the memory. This function is thread-safe.

- Return values: OK or some other error code

- Example of usage:

```C++
    #include "persistentKeyValuePairs.h"
    persistentKeyValuePairs<int, String> pkvpA;

    int e = pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");

    if (e) ... handle the error
```

### int size ()

Class member function returns the number of key-value pairs currently in the database.


### errorCode FindBlockOffset (keyType key, uint32_t& blockOffset)

Class member function searches for a key (and data file block offset associated with it). It only accesses the memory. This function is thread-safe.

- Arguments: key (input), blockOffset reference (output)

- Return values: OK or NOT_FOUND or some other error code

- Example of usage:

```C++
    #include "persistentKeyValuePairs.h"
    persistentKeyValuePairs<int, String> pkvpA;

    pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");

    uint32_t blockOffset;
    int e = pkvpA.FindBlockOffset (8, blockOffset);
    switch (e) {
        case pkvpA.OK: ... handle found condition
        case pkvpA.NOT_FOUND: ... handle not found condition
        default: ... handle the error
    }
```


###  errorCode FindValue (keyType key, valueType *value, uint32_t blockOffset)

Class member function searches for a value associated with the key. It accesses the disk where the value is stored. This function is thread-safe.

- Arguments: key (input), value reference (output), optional blockOffset (input) for faster searching if it is already known

- Return values: OK or NOT_FOUND or some other error code

- Example of usage:

```C++
    #include "persistentKeyValuePairs.h"
    persistentKeyValuePairs<int, String> pkvpA;

    pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");

    String value;
    int e = pkvpA.FindValue (8, &value);
    switch (e) {
        case pkvpA.OK: ... handle found condition
        case pkvpA.NOT_FOUND: ... handle not found condition
        default: ... handle the error
    }
```


### errorCode Insert (keyType key, valueType value)

Class member function inserts the new key-value pair into the database. This function is thread-safe.

- Return values: OK or some other error code

- Example of usage:

```C++
    #include "persistentKeyValuePairs.h"
    persistentKeyValuePairs<int, String> pkvpA;

    pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");

    int e = pkvpA.Insert (8, "eight");
    
    if (e) ... handle the error
```


### (1) errorCode Update (keyType key, valueType newValue, uint32_t *pBlockOffset)

Class member function updates the value associated with the key. This function is thread-safe.

- Arguments: key (input), newValue (input), optional pBlockOffset (input) for faster searching if it is already known

- Return values: OK, NOT_FOUND or some other error code

- Example of usage:

```C++
    #include "persistentKeyValuePairs.h"
    persistentKeyValuePairs<int, String> pkvpA;

    pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");

    int e = pkvpA.Update (8, "eighty"); 
    
    if (e) ... handle the error
```

### (2) errorCode Update (keyType key, void (*updateCallback) (valueType &value), uint32_t *pBlockOffset)

Class member function updates the value associated with the key. It uses a callback function that can calculate a new value from the old one. This function is thread-safe.

- Arguments: key (input), updateCallback (callback function), optional pBlockOffset (input) for faster searching if it is already known

- Return values: OK, NOT_FOUND or some other error code

- Example of usage:

```C++
    #include "persistentKeyValuePairs.h"
    persistentKeyValuePairs<int, String> pkvpA;

    pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");

    int e = pkvpA.Update (8, [] (String& value) { value.toUpperCase (); } ); 
    
    if (e) ... handle the error
```


### (1) errorCode Upsert (keyType key, valueType newValue)

Class member function updates the value associated with the key or inserts if the key does not exist yet. This function is thread-safe.

- Arguments: key (input), newValue (input)

- Return values: OK or some other error code

- Example of usage:

```C++
    #include "persistentKeyValuePairs.h"
    persistentKeyValuePairs<int, String> pkvpA;

    pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");

    int e = pkvpA.Upsert (8, "eighteen"); 
    
    if (e) ... handle the error
```

### (2) errorCode Upsert (keyType key, void (*updateCallback) (valueType &value), valueType defaultValue)

Class member function updates the value associated with the key or inserts defaultValue if the key does not exist yet. It uses a callback function that can calculate a new value from the old one. This function is thread-safe.

- Arguments: key (input), updateCallback (callback function), defaulValue (input)

- Return values: OK or some other error code

- Example of usage:

```C++
    #include "persistentKeyValuePairs.h"
    persistentKeyValuePairs<int, String> pkvpA;

    pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");

    int e = pkvpA.Upsert (8, [] (String& value) { value += "y"; }, "eight" ); 
    
    if (e) ... handle the error
```


### errorCode Delete (keyType key)

Class member function deletes the key and the value associated with it. This function is thread-safe.

- Arguments: key (input)

- Return values: OK or some other error code

- Example of usage:

```C++
    #include "persistentKeyValuePairs.h"
    persistentKeyValuePairs<int, String> pkvpA;

    pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");

    int e = pkvpA.Delete (8); 
    
    if (e) ... handle the error
```


### errorCode Truncate ()

Class member function deletes all the keys and the values associated with them. This function is thread-safe.

- Return values: OK or some other error code

- Example of usage:

```C++
    #include "persistentKeyValuePairs.h"
    persistentKeyValuePairs<int, String> pkvpA;

    pkvpA.loadData ("/var/persistentKeyValuePairs/A.kvp");

    int e = pkvpA.Truncate (); 
    
    if (e) ... handle the error
```


### Locking mechanism

- void Lock () (class member function): recursively locks (prevents access to certain class member functions) to part of code that do not allow simultaneous access from multiple threads
- void Unlock () (class member function): recursively unlocks (access to certain class member functions) to part of code that do not allow simultaneous access from multiple threads


### Iterator

Class member iterator iterates through memory structure (binary balanced search tree) of keys. If the values are needed as well, they must be fetched from the disk separately (using FindValue member function).

- Example of usage:

```C++
    pkvpA.clearLastErrorCode ();
    for (auto p: pkvpA) {
        String value;
        e = pkvpA.FindValue (p.key, &value, p.blockOffset);
        if (!e) {
            Serial.print (p.key); Serial.print (" - "); Serial.println (value);
        } else {
             ... handle the error
             break;
        }
    }    
    if (pkvpA.lastErrorCode) ... handle the error
```


## Some thoughts and numbers

### How many persistent key-value pairs can reside in the controller?

Well, it depends, but if we make some assumptions we can give some answers. Say persistentKeyValuePairs is running on ESP32 with a FAT partition scheme having 1.2 MB app memory and relatively small 16 bit integer keys. Then up to 5200 keys can fit into the controller's heap memory. Values, on the other hand, are only kept on the flash disk.

### How fast the insert and find operations are?

Time complexity of a balanced binary search tree implemented beneath persistent key-value pairs can be estimated by O (log n), where n is the number of key-value pairs already inserted. But the max number of key-value pairs is relatively small so the speed of disk IO operations prevails. LittleFS file system uses less memory and leaves more of it to the persistent key-value pairs. It performs better than FAT with inserts but worse with finds. Here are some measurements:

![insert_find_times](insert_find_times.gif)

# Simple Arduino key-value database (ESP32 boards with flash disk)


Persistent key-value pairs library that offers a kind of structured data storage. It can be used as a small and simple database. 


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
        Serial.print (p->key); Serial.print (", "); Serial.print (p->blockOffset); Serial.print (" -> "); 
        
        // values are read from disk, obtaining a value may be much slower
        String value;
        e = pkvpA.FindValue (p->key, &value, p->blockOffset);
        if (e == pkvpA.OK) 
            Serial.println (value);
        else
            Serial.printf ("Error: %s\n", pkvpA.errorCodeText (e));
    }
```


## Some thoughts and numbers

### How many persistent key-value pairs can reside in the controller?

Well, it depends, but if we make some assumptions we can give some answers. Say persistentKeyValuePairs is running on ESP32 with a FAT partition scheme having 1.2 MB app memory and relatively small 16 bit integer keys. Then up to 5200 keys can fit into the controller's heap memory. Values, on the other hand, are only kept on the flash disk.

### How fast the insert and find operations are?

Time complexity of a balanced binary search tree implemented beneath persistent key-value pairs can be estimated by O (log n), where n is the number of key-value pairs already inserted. But the max number of key-value pairs is relatively small so the speed of disk IO operations prevails. LittleFS file system uses less memory and leaves more of it to the persistent key-value pairs. It performs better than FAT with inserts but worse with finds. Here are some measurements:

![insert_find_times](insert_find_times.gif)

/*
 * persistentKeyValuePairs.h for Arduino ESP boards
 * 
 * This file is part of Persistent-key-value-pairs-for-Arduino: https://github.com/BojanJurca/Cplusplus-persistent-key-value-pairs-for-Arduino
 *
 * Persistent key-value pairs may be used as a simple database with the following functions (see examples in BasicUsage.ino):
 *
 *    - Insert (key, value)                             - inserts a new key-value pair
 *
 *    - FindBlockOffset (key)                           - searches (memory) keyValuePairs for key
 *    - FindValue (key, optional block offset)          - searches (memory) keyValuePairs for blockOffset connected to key and then it reads the value from (disk) data file 
 *                                                        (it works slightly faster if block offset is already known, such as during iterations)
 *
 *    - Update (key, new value, optional block offset)  - updates the value associated by the key
 *                                                        (it works slightly faster if block offset is already known, such as during iterations)
 *
 *    - Delete (key)                                    - deletes key-value pair identified by the key
 *    - Truncate                                        - deletes all key-value pairs
 *
 *    - Iterate                                         - iterate (list) through all the keys and their blockOffsets with an Iterator
 * 
 * Data storage structure used for persistentKeyValuePairs:
 *
 * (disk) persistentKeyValuePairs consists of:
 *    - data file
 *    - (memory) keyValuePairs that keep keys and pointers (offsets) to data in the data file
 *    - (memory) vector that keeps pointers (offsets) to free blocks in the data file
 *    - semaphore to synchronize (possible) multi-tasking accesses to persistentKeyValuePairs
 *
 *    (disk) data file structure:
 *       - data file consists consecutive of blocks
 *       - Each block starts with int16_t number which denotes the size of the block (in bytes). If the number is positive the block is considered to be used
 *         with useful data, if the number is negative the block is considered to be deleted (free). Positive int16_t numbers can vary from 0 to 32768, so
 *         32768 is the maximum size of a single data block.
 *       - after the block size number, a key and its value are stored in the block (only if the block is beeing used).
 *
 *    (memory) keyValuePairs structure:
 *       - the key is the same key as used for persistentKeyValuePairs
 *       - the value is an offset to data file block containing the data, persistentKeyValuePairs' value will be fetched from there. Data file offset is
 *         stored in uint32_t so maximum data file offest can theoretically be 4294967296, but ESP32 files can't be that large.
 *
 *    (memory) vector structure:
 *       - a free block list vector contains structures with:
 *            - data file offset (uint16_t) of a free block
 *            - size of a free block (int16_t)
 * 
 * Bojan Jurca, October 10, 2023
 *  
 */


#ifndef __PERSISTENT_KEY_VALUE_PAIRS_H__
    #define __PERSISTENT_KEY_VALUE_PAIRS_H__


    // TUNNING PARAMETERS

    #define __PERSISTENT_KEY_VALUE_PAIRS_PCT_FREE__ 0.2 // how much space is left free in data block to let data "breed" a little - only makes sense for String values 

    
    #include "keyValuePairs.h"
    #include "vector.h"

    // #define __PERSISTENT_KEY_VALUE_PAIR_H_EXCEPTIONS__   // uncomment this line if you want keyValuePairs to throw exceptions
    // #define __PERSISTENT_KEY_VALUE_PAIRS_H_DEBUG__       // uncomment this line for debugging puroposes

    #ifdef __PERSISTENT_KEY_VALUE_PAIRS_H_DEBUG__
        #define __persistent_key_value_pairs_h_debug__(X) { Serial.print("__persistent_key_value_pairs_h_debug__: ");Serial.println(X); }
    #else
        #define __persistent_key_value_pairs_h_debug__(X) ;
    #endif


    static SemaphoreHandle_t __persistentKeyValuePairsSemaphore__ = xSemaphoreCreateMutex (); 

    template <class keyType, class valueType> class persistentKeyValuePairs : private keyValuePairs<keyType, uint32_t> {
  
        public:

            enum errorCode {OK = 0, // not all error codes are needed here but they are shared among keyValuePairs and vectors as well 
                            NOT_FOUND = -1,               // key is not found
                            BAD_ALLOC = -2,               // out of memory
                            OUT_OF_RANGE = -3,            // invalid index
                            NOT_UNIQUE = -4,              // the key is not unique
                            DATA_CHANGED = -5,            // unexpected data value found
                            FILE_IO_ERROR = -6,           // file operation error
                            NOT_WHILE_ITERATING = -7      // operation can not be berformed while iterating
            }; // note that all errors are negative numbers

            char *errorCodeText (errorCode e) {
                switch (e) {
                    case OK:                  return (char *) "OK";
                    case NOT_FOUND:           return (char *) "NOT_FOUND";
                    case BAD_ALLOC:           return (char *) "BAD_ALLOC";
                    case OUT_OF_RANGE:        return (char *) "OUT_OF_RANGE";
                    case NOT_UNIQUE:          return (char *) "NOT_UNIQUE";
                    case DATA_CHANGED:        return (char *) "DATA_CHANGED";
                    case FILE_IO_ERROR:       return (char *) "FILE_IO_ERROR";
                    case NOT_WHILE_ITERATING: return (char *) "NOT_WHILE_ITERATING";
                }
                return NULL; // doesn't happen
            }

            errorCode lastErrorCode = OK;

            void clearLastErrorCode () { lastErrorCode = OK; }


           /*
            *  Constructor of persistentKeyValuePairs that does not load the data. Subsequential call to loadData function is needed:
            *  
            *     persistentKeyValuePairs<int, String> pkvpA;
            *
            *     void setup () {
            *         Serial.begin (115200);
            *
            *         fileSystem.mountFAT (true);    
            *
            *         kvp.loadData ("/persistentKeyValuePairs/A.kvp");
            *
            *          ...
            */
            
            persistentKeyValuePairs () { return; }

           /*
            *  Constructor of persistentKeyValuePairs that also loads the data from data file: 
            *  
            *     persistentKeyValuePairs<int, String> pkvpA ("/persistentKeyValuePairs/A.kvp");
            *     if (pkvpA.lastErrorCode != pkvpA.OK) 
            *         Serial.printf ("pkvpA constructor failed: %s, all the data may not be indexed\n", pkvpA.errorCodeText (pkvpA.lastErrorCode));
            *
            */
            
            persistentKeyValuePairs (const char *dataFileName) { 
                loadData (dataFileName);
            }

            ~persistentKeyValuePairs () { 
                if (__dataFile__)
                    __dataFile__.close ();
            } 


           /*
            *  Loads the data from data file.
            *  
            */

            errorCode loadData (const char *dataFileName) {
                xSemaphoreTakeRecursive (__semaphore__, portMAX_DELAY); // don't allow other tasks to change keyValuePairs structure or write to __dataFile__ meanwhile

                // clear existing data

                if (__dataFile__)
                    __dataFile__.close ();

                keyValuePairs<keyType, uint32_t>::clear ();
                __freeBlocksList__.clear ();

                // load new data

                strcpy (__dataFileName__, dataFileName);

                if (!fileSystem.isFile (dataFileName)) {
                    __dataFile__ = fileSystem.open (dataFileName, "w", true);
                    if (__dataFile__) {
                          __dataFile__.close (); 
                    } else {
                        __persistent_key_value_pairs_h_debug__ ("load: failed creating the empty data file.");
                        xSemaphoreGiveRecursive (__semaphore__);
                        { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                    }
                }

                __dataFile__ = fileSystem.open (dataFileName, "r+", false);
                if (!__dataFile__) {
                    lastErrorCode = FILE_IO_ERROR;
                    __persistent_key_value_pairs_h_debug__ ("load: failed opening the data file.");
                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }                    
                }

                __dataFileSize__ = __dataFile__.size (); 
                uint64_t blockOffset = 0;

                while (blockOffset < __dataFileSize__ &&  blockOffset <= 0xFFFFFFFF) { // max uint32_t
                    int16_t blockSize;
                    keyType key;
                    valueType value;

                    if (!__readBlock__ (blockSize, key, value, (uint32_t) blockOffset, true)) {
                        __persistent_key_value_pairs_h_debug__ ("load: __readBlock__ failed.");
                        xSemaphoreGiveRecursive (__semaphore__);
                        { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }                         
                    }
                    if (blockSize > 0) { // block containining the data -> insert into keyValuePairs
                        __persistent_key_value_pairs_h_debug__ ("constructor: blockOffset: " + String (blockOffset));
                        __persistent_key_value_pairs_h_debug__ ("constructor: used blockSize: " + String (blockSize));
                        __persistent_key_value_pairs_h_debug__ ("constructor: used key-value: " + String (key) + "-" + String (value));
                        if (keyValuePairs<keyType, uint32_t>::insert (key, (uint32_t) blockOffset) != keyValuePairs<keyType, uint32_t>::OK) {
                            __persistent_key_value_pairs_h_debug__ ("load: insert failed, error " + String (keyValuePairs<keyType, uint32_t>::lastErrorCode));
                            errorCode e = lastErrorCode = (errorCode) keyValuePairs<keyType, uint32_t>::lastErrorCode;
                            xSemaphoreGiveRecursive (__semaphore__);
                            return e;
                        }
                    } else { // free block -> insert into __freeBlockList__
                        __persistent_key_value_pairs_h_debug__ ("load: blockOffset: " + String (blockOffset));
                        __persistent_key_value_pairs_h_debug__ ("load: free blockSize: " + String (-blockSize));
                        blockSize = (int16_t) -blockSize;
                        if (__freeBlocksList__.push_back ( {(uint32_t) blockOffset, blockSize} ) != __freeBlocksList__.OK) {
                            __persistent_key_value_pairs_h_debug__ ("load: push_back failed.");
                            errorCode e = lastErrorCode = (errorCode) __freeBlocksList__.lastErrorCode;
                            xSemaphoreGiveRecursive (__semaphore__);
                            return e;
                        }
                    } 

                    blockOffset += blockSize;
                }

                xSemaphoreGiveRecursive (__semaphore__);
                return OK;
            }


           /*
            * Returns the number of key-value pairs.
            */

            int size () { return keyValuePairs<keyType, uint32_t>::size (); }


           /*
            *  Inserts a new key-value pair, returns OK or one of the error codes.
            */

            errorCode Insert (keyType key, valueType value) {

                if (!__dataFile__) { 
                    __persistent_key_value_pairs_h_debug__ ("Insert: __dataFile__ not opened.");
                    lastErrorCode = FILE_IO_ERROR; 
                    return FILE_IO_ERROR; 
                }

                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!key) {                                                                                               // ... check if parameter construction is valid
                        __persistent_key_value_pairs_h_debug__ ("Insert: key constructor failed.");
                        { lastErrorCode = BAD_ALLOC; return BAD_ALLOC; }                                                      // report error if it is not
                    }
                if (std::is_same<valueType, String>::value)                                                                   // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!value) {                                                                                             // ... check if parameter construction is valid
                        __persistent_key_value_pairs_h_debug__ ("Insert: value constructor failed.");
                        { lastErrorCode = BAD_ALLOC; return BAD_ALLOC; }                                                      // report error if it is not
                    }

                xSemaphoreTakeRecursive (__semaphore__, portMAX_DELAY); // don't allow other tasks to change keyValuePairs structure or write to __dataFile__ meanwhile
                if (__inIteration__) {
                    __persistent_key_value_pairs_h_debug__ ("Insert: can not insert while iterating.");
                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = NOT_WHILE_ITERATING; return NOT_WHILE_ITERATING; }
                }

                // 1. get ready for writting into __dataFile__
                size_t dataSize = sizeof (int16_t); // block size information
                size_t blockSize = dataSize;
                if (std::is_same<keyType, String>::value) { // if value is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    dataSize += (((String *) &key)->length () + 1); // add 1 for closing 0
                    blockSize += (((String *) &key)->length () + 1) + (((String *) &key)->length () + 1) * __PERSISTENT_KEY_VALUE_PAIRS_PCT_FREE__ + 0.5; // add PCT_FREE for Strings
                } else { // fixed size key
                    dataSize += sizeof (keyType);
                    blockSize += sizeof (keyType);
                }                
                if (std::is_same<valueType, String>::value) { // if value is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    dataSize += (((String *) &value)->length () + 1); // add 1 for closing 0
                    blockSize += (((String *) &value)->length () + 1) + (((String *) &value)->length () + 1) * __PERSISTENT_KEY_VALUE_PAIRS_PCT_FREE__ + 0.5; // add PCT_FREE for Strings
                } else { // fixed size value
                    dataSize += sizeof (valueType);
                    blockSize += sizeof (valueType);
                }
                if (blockSize > 32768) {
                    __persistent_key_value_pairs_h_debug__ ("Insert: dataSize too large.");
                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = BAD_ALLOC; return BAD_ALLOC; }      
                }

                // 2. search __freeBlocksList__ for most suitable free block, if it exists
                int freeBlockIndex = -1;
                uint32_t minWaste = 0xFFFFFFFF;
                for (int i = 0; i < __freeBlocksList__.size (); i ++) {
                    if (__freeBlocksList__ [i].blockSize >= dataSize && __freeBlocksList__ [i].blockSize - dataSize < minWaste) {
                        freeBlockIndex = i;
                        minWaste = __freeBlocksList__ [i].blockSize - dataSize;
                    }
                }

                // 3. reposition __dataFile__ pointer
                uint32_t blockOffset;                
                if (freeBlockIndex == -1) { // append data to the end of __dataFile__
                    __persistent_key_value_pairs_h_debug__ ("Insert: appending data to the end of data file, blockOffst: " + String (blockOffset));
                    blockOffset = __dataFileSize__;
                } else { // writte data to free block in __dataFile__
                    __persistent_key_value_pairs_h_debug__ ("Insert: writing to free block " + String (freeBlockIndex) + ", blockOffst: " + String (blockOffset) + ", blockSize: " + String (__freeBlocksList__ [freeBlockIndex].blockSize));
                    blockOffset = __freeBlocksList__ [freeBlockIndex].blockOffset;
                    blockSize = __freeBlocksList__ [freeBlockIndex].blockSize;
                }
                if (!__dataFile__.seek (blockOffset, SeekSet)) {
                    __persistent_key_value_pairs_h_debug__ ("Insert: seek failed (3).");
                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; } 
                }

                // 4. update (memory) keyValuePairs structure 
                if (keyValuePairs<keyType, uint32_t>::insert (key, blockOffset) != keyValuePairs<keyType, uint32_t>::OK) {
                    __persistent_key_value_pairs_h_debug__ ("Insert: insert failed (4).");
                    errorCode e = lastErrorCode = (errorCode) keyValuePairs<keyType, uint32_t>::lastErrorCode;
                    xSemaphoreGiveRecursive (__semaphore__);
                    return e;
                }

                // 5. construct the block to be written
                byte *block = (byte *) malloc (blockSize);
                if (!block) {
                    __persistent_key_value_pairs_h_debug__ ("Insert: malloc failed (5).");
                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = BAD_ALLOC; return BAD_ALLOC; }
                }

                int16_t i = 0;
                int16_t bs = (int16_t) blockSize;
                memcpy (block + i, &bs, sizeof (bs)); i += sizeof (bs);
                if (std::is_same<keyType, String>::value) { // if value is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    size_t l = ((String *) &key)->length () + 1; // add 1 for closing 0
                    memcpy (block + i, ((String *) &key)->c_str (), l); i += l;
                } else { // fixed size key
                    memcpy (block + i, &key, sizeof (key)); i += sizeof (key);
                }       
                if (std::is_same<valueType, String>::value) { // if value is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    size_t l = ((String *) &value)->length () + 1; // add 1 for closing 0
                    memcpy (block + i, ((String *) &value)->c_str (), l); i += l;
                } else { // fixed size value
                    memcpy (block + i, &value, sizeof (value)); i += sizeof (value);
                }

                // 6. write block to __dataFile__
                if (__dataFile__.write (block, blockSize) != blockSize) {
                    __persistent_key_value_pairs_h_debug__ ("Insert: write failed (6).");
                    free (block);

                    // 7. (try to) roll-back
                    if (__dataFile__.seek (blockOffset, SeekSet)) {
                        blockSize = (int16_t) -blockSize;
                        if (__dataFile__.write ((byte *) &blockSize, sizeof (blockSize)) != sizeof (blockSize)) { // can't roll-back
                            __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                        }
                    } else { // can't roll-back
                        __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                    }

                    keyValuePairs<keyType, uint32_t>::erase (key);

                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                }
                free (block);

                // 7. roll-out
                if (freeBlockIndex == -1) { // data appended to the end of __dataFile__
                    __dataFileSize__ += blockSize;
                } else { // data written to free block in __dataFile__
                    __freeBlocksList__.erase (freeBlockIndex); // doesn't fail
                }

                __persistent_key_value_pairs_h_debug__ ("Insert: OK.");
                xSemaphoreGiveRecursive (__semaphore__);
                return OK;
            }


           /*
            *  Retrieve blockOffset from (memory) keyValuePairs, so it is fast.
            */

            errorCode FindBlockOffset (keyType key, uint32_t& blockOffset) {
                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!key) {                                                                                               // ... check if parameter construction is valid
                        __persistent_key_value_pairs_h_debug__ ("FindBlockOffset: key constructor failed.");
                        { lastErrorCode = BAD_ALLOC; return BAD_ALLOC; }
                    }

                xSemaphoreTakeRecursive (__semaphore__, portMAX_DELAY); // don't allow other tasks to change keyValuePairs structure or write to __dataFile__ meanwhile
                keyValuePairs<keyType, uint32_t>::clearLastErrorCode ();
                uint32_t *p = keyValuePairs<keyType, uint32_t>::find (key);
                if (p) { // if found
                    blockOffset = *p;
                    xSemaphoreGiveRecursive (__semaphore__); 
                    return OK;
                } else { // not found or error
                    if (keyValuePairs<keyType, uint32_t>::lastErrorCode == keyValuePairs<keyType, uint32_t>::OK) {
                        __persistent_key_value_pairs_h_debug__ ("FindBlockOffset: NOT_FOUND.");
                        xSemaphoreGiveRecursive (__semaphore__); 
                        return NOT_FOUND;
                    } else {
                        errorCode e = lastErrorCode = (errorCode) keyValuePairs<keyType, uint32_t>::lastErrorCode;
                        __persistent_key_value_pairs_h_debug__ ("FindBlockOffset error " + String (lastErrorCode));
                        xSemaphoreGiveRecursive (__semaphore__); 
                        return e;
                    }
                }
            }


           /*
            *  Read the value from (disk) __dataFile__, so it is slow. 
            */

            errorCode FindValue (keyType key, valueType *value, uint32_t blockOffset = 0xFFFFFFFF) { 
                if (!__dataFile__) { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; } 

                keyType storedKey = {};

                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!key) {                                                                                               // ... check if parameter construction is valid
                        __persistent_key_value_pairs_h_debug__ ("FindValue: key constructor failed.");
                        { lastErrorCode = BAD_ALLOC; return BAD_ALLOC; }
                    }

                xSemaphoreTakeRecursive (__semaphore__, portMAX_DELAY); // don't allow other tasks to change keyValuePairs structure or write to __dataFile__ meanwhile

                if (blockOffset == 0xFFFFFFFF) { // if block offset was not specified find it from keyValuePairs
                    keyValuePairs<keyType, uint32_t>::clearLastErrorCode ();
                    uint32_t *pBlockOffset = keyValuePairs<keyType, uint32_t>::find (key);
                    if (!pBlockOffset) { // if not found or error
                        if (keyValuePairs<keyType, uint32_t>::lastErrorCode == keyValuePairs<keyType, uint32_t>::OK) {
                            __persistent_key_value_pairs_h_debug__ ("FindValue: NOT_FOUND.");
                            xSemaphoreGiveRecursive (__semaphore__); 
                            return NOT_FOUND;
                        } else {
                            errorCode e = lastErrorCode = (errorCode) keyValuePairs<keyType, uint32_t>::lastErrorCode;
                            __persistent_key_value_pairs_h_debug__ ("FindValue: error " + String (lastErrorCode));
                            xSemaphoreGiveRecursive (__semaphore__); 
                            return e;
                        }
                    }
                    blockOffset = *pBlockOffset;
                }

                int16_t blockSize;
                if (__readBlock__ (blockSize, storedKey, *value, blockOffset)) {
                    if (blockSize > 0 && storedKey == key) {
                        xSemaphoreGiveRecursive (__semaphore__); 
                        return OK; // success  
                    } else {
                        __persistent_key_value_pairs_h_debug__ ("FindValue: DATA_CHANGED.");
                        xSemaphoreGiveRecursive (__semaphore__); 
                        { lastErrorCode = DATA_CHANGED; return DATA_CHANGED; } // shouldn't happen, but check anyway ...
                    }
                } else {
                    __persistent_key_value_pairs_h_debug__ ("FindValue: __readBlock__ error");
                    errorCode e = lastErrorCode;
                    xSemaphoreGiveRecursive (__semaphore__); 
                    return e; 
                } 
            }


           /*
            *  Updates the value associated with the key
            */

            errorCode Update (keyType key, valueType newValue, uint32_t *PblockOffset = NULL) {
                if (!__dataFile__) { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }

                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!key) {                                                                                               // ... check if parameter construction is valid
                        __persistent_key_value_pairs_h_debug__ ("Update: key constructor failed.");
                        { lastErrorCode = BAD_ALLOC; return BAD_ALLOC; }
                    }
                if (std::is_same<valueType, String>::value)                                                                   // if value is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!newValue) {                                                                                          // ... check if parameter construction is valid
                        __persistent_key_value_pairs_h_debug__ ("Update: value constructor failed.");
                        { lastErrorCode = BAD_ALLOC; return BAD_ALLOC; }
                    }

                xSemaphoreTakeRecursive (__semaphore__, portMAX_DELAY); // don't allow other tasks to change keyValuePairs structure or write to __dataFile__ meanwhile

                // 1. get blockOffset
                keyValuePairs<keyType, uint32_t>::clearLastErrorCode ();
                uint32_t *blockOffset = keyValuePairs<keyType, uint32_t>::find (key);
                if (!blockOffset) { // if not found
                    if (keyValuePairs<keyType, uint32_t>::lastErrorCode == keyValuePairs<keyType, uint32_t>::OK) {
                        __persistent_key_value_pairs_h_debug__ ("Update: NOT_FOUND.");
                        xSemaphoreGiveRecursive (__semaphore__); 
                        return NOT_FOUND;
                    } else {
                        errorCode e = lastErrorCode = (errorCode) keyValuePairs<keyType, uint32_t>::lastErrorCode;
                        xSemaphoreGiveRecursive (__semaphore__); 
                        return e;
                    }
                }

                // 2. read the block size and stored key
                int16_t blockSize;
                size_t newBlockSize;
                keyType storedKey;
                valueType storedValue;
                if (!__readBlock__ (blockSize, storedKey, storedValue, *blockOffset, true)) {
                    __persistent_key_value_pairs_h_debug__ ("Update: __readBlock__ failed.");
                    errorCode e = lastErrorCode;
                    xSemaphoreGiveRecursive (__semaphore__); 
                    return e;
                }
                if (blockSize <= 0 || storedKey != key) {
                    __persistent_key_value_pairs_h_debug__ ("Update: DATA_CHANGED.");
                    xSemaphoreGiveRecursive (__semaphore__); 
                    { lastErrorCode = DATA_CHANGED; return DATA_CHANGED; } // shouldn't happen, but check anyway ...
                }

                // 3. calculate new block and data size
                size_t dataSize = sizeof (int16_t); // block size information
                newBlockSize = dataSize;
                if (std::is_same<keyType, String>::value) { // if value is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    dataSize += (((String *) &key)->length () + 1); // add 1 for closing 0
                    newBlockSize += (((String *) &key)->length () + 1) + (((String *) &key)->length () + 1) * __PERSISTENT_KEY_VALUE_PAIRS_PCT_FREE__ + 0.5; // add PCT_FREE for Strings
                } else { // fixed size key
                    dataSize += sizeof (keyType);
                    newBlockSize += sizeof (keyType);
                }                
                if (std::is_same<valueType, String>::value) { // if value is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    dataSize += (((String *) &newValue)->length () + 1); // add 1 for closing 0
                    newBlockSize += (((String *) &newValue)->length () + 1) + (((String *) &newValue)->length () + 1) * __PERSISTENT_KEY_VALUE_PAIRS_PCT_FREE__ + 0.5; // add PCT_FREE for Strings
                } else { // fixed size value
                    dataSize += sizeof (valueType);
                    newBlockSize += sizeof (valueType);
                }
                if (newBlockSize > 32768) {
                    __persistent_key_value_pairs_h_debug__ ("Update: dataSize too large.");
                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = BAD_ALLOC; return BAD_ALLOC; }      
                }

                // 4. decide where to write the new value: existing block or a new one
                if (dataSize <= blockSize) { // there is enough space for new data in the existing block - easier case
                    __persistent_key_value_pairs_h_debug__ ("Update: writing to the same block: " + String (dataSize) + ", " + String (blockSize));
                    uint32_t dataFileOffset = *blockOffset + sizeof (int16_t); // skip block size information
                    if (std::is_same<keyType, String>::value) { // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                        dataFileOffset += (((String *) &key)->length () + 1); // add 1 for closing 0
                    } else { // fixed size key
                        dataFileOffset += sizeof (keyType);
                    }                

                    // 5. write new value to __dataFile__
                    if (!__dataFile__.seek (dataFileOffset, SeekSet)) {
                        __persistent_key_value_pairs_h_debug__ ("Update: seek failed (5).");
                        xSemaphoreGiveRecursive (__semaphore__); 
                        { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                    }
                    int bytesToWrite;
                    int bytesWritten;
                    if (std::is_same<valueType, String>::value) { // if value is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                        bytesToWrite = (((String *) &newValue)->length () + 1);
                        bytesWritten = __dataFile__.write ((byte *) ((String *) &newValue)->c_str () , bytesToWrite);
                    } else {
                        bytesToWrite = sizeof (newValue);
                        bytesWritten = __dataFile__.write ((byte *) &newValue , bytesToWrite);
                    }
                    if (bytesWritten != bytesToWrite) { // file IO error, it is highly unlikely that rolling-back to the old value would succeed
                        __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                        xSemaphoreGiveRecursive (__semaphore__); 
                        { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                    }

                    // success

                } else { // existing block is not big eneugh, we'll need a new block - more difficult case
                    __persistent_key_value_pairs_h_debug__ ("Update: writing to existing block: " + String (dataSize) + " > " + String (blockSize) + " new block size: " + String (newBlockSize));

                    // 6. search __freeBlocksList__ for most suitable free block, if it exists
                    int freeBlockIndex = -1;
                    uint32_t minWaste = 0xFFFFFFFF;
                    for (int i = 0; i < __freeBlocksList__.size (); i ++) {
                        if (__freeBlocksList__ [i].blockSize >= dataSize && __freeBlocksList__ [i].blockSize - dataSize < minWaste) {
                            freeBlockIndex = i;
                            minWaste = __freeBlocksList__ [i].blockSize - dataSize;
                        }
                    }

                    // 7. reposition __dataFile__ pointer
                    uint32_t newBlockOffset;                
                    if (freeBlockIndex == -1) { // append data to the end of __dataFile__
                        __persistent_key_value_pairs_h_debug__ ("Update: writing to new block, dataSize: " + String (dataSize) + ", blockSize: " + String (newBlockSize));
                        newBlockOffset = __dataFileSize__;
                    } else { // writte data to free block in __dataFile__
                        __persistent_key_value_pairs_h_debug__ ("Update: writing to free block: " + String (dataSize) + ", " + String (__freeBlocksList__ [freeBlockIndex].blockSize));
                        newBlockOffset = __freeBlocksList__ [freeBlockIndex].blockOffset;
                        newBlockSize = __freeBlocksList__ [freeBlockIndex].blockSize;
                    }
                    if (!__dataFile__.seek (newBlockOffset, SeekSet)) {
                        __persistent_key_value_pairs_h_debug__ ("Update: seek failed (7).");
                        xSemaphoreGiveRecursive (__semaphore__);
                        { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                    }

                    // 8. construct the block to be written
                    byte *block = (byte *) malloc (newBlockSize);
                    if (!block) {
                        __persistent_key_value_pairs_h_debug__ ("Update: malloc failed (8).");
                        xSemaphoreGiveRecursive (__semaphore__);
                        { lastErrorCode = BAD_ALLOC; return BAD_ALLOC; }
                    }

                    int16_t i = 0;
                    int16_t bs = (int16_t) newBlockSize;
                    memcpy (block + i, &bs, sizeof (bs)); i += sizeof (bs);
                    if (std::is_same<keyType, String>::value) { // if value is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                        size_t l = ((String *) &key)->length () + 1; // add 1 for closing 0
                        memcpy (block + i, ((String *) &key)->c_str (), l); i += l;
                    } else { // fixed size key
                        memcpy (block + i, &key, sizeof (key)); i += sizeof (key);
                    }       
                    if (std::is_same<valueType, String>::value) { // if value is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                        size_t l = ((String *) &newValue)->length () + 1; // add 1 for closing 0
                        memcpy (block + i, ((String *) &newValue)->c_str (), l); i += l;
                    } else { // fixed size value
                        memcpy (block + i, &newValue, sizeof (newValue)); i += sizeof (newValue);
                    }

                    // 9. write new block to __dataFile__
                    if (__dataFile__.write (block, dataSize) != dataSize) {
                        __persistent_key_value_pairs_h_debug__ ("Update: write failed (9).");
                        free (block);

                        // 10. (try to) roll-back
                        if (__dataFile__.seek (newBlockOffset, SeekSet)) {
                            newBlockSize = (int16_t) -newBlockSize;
                            if (__dataFile__.write ((byte *) &newBlockSize, sizeof (newBlockSize)) != sizeof (newBlockSize)) { // can't roll-back
                                __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                            }
                        } else { // can't roll-back
                            __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                        }
                        xSemaphoreGiveRecursive (__semaphore__);
                        { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                    }
                    free (block);

                    // 11. roll-out
                    if (freeBlockIndex == -1) { // data appended to the end of __dataFile__
                        __dataFileSize__ += newBlockSize;
                    } else { // data written to free block in __dataFile__
                        __freeBlocksList__.erase (freeBlockIndex); // doesn't fail
                    }
                    // mark old block as free
                    if (!__dataFile__.seek (*blockOffset, SeekSet)) {
                        __persistent_key_value_pairs_h_debug__ ("Update: seek failed (11).");
                        __dataFile__.close (); // data file is corrupt (it contains two entries with the same key) and it si not likely we can roll it back
                        xSemaphoreGiveRecursive (__semaphore__);
                        { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                    }
                    blockSize = (int16_t) -blockSize;
                    if (__dataFile__.write ((byte *) &blockSize, sizeof (blockSize)) != sizeof (blockSize)) {
                        __persistent_key_value_pairs_h_debug__ ("Update: write failed (12).");
                        __dataFile__.close (); // data file is corrupt (it contains two entries with the same key) and it si not likely we can roll it back
                        xSemaphoreGiveRecursive (__semaphore__);
                        { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                    }
                    // update keyValuePairs information
                    *blockOffset = newBlockOffset; // there is no reason this would fail
                    // update __freeBlocklist__
                    if (__freeBlocksList__.push_back ( {*blockOffset, blockSize} ) != __freeBlocksList__.OK) {
                        __persistent_key_value_pairs_h_debug__ ("Update: push_back failed (12).");
                        // it is not really important to return with an error here, persistentKeyValuePairs can continue working with this error 
                        // xSemaphoreGiveRecursive (__semaphore__); 
                        // return keyValuePairs<keyType, uint32_t>::lastErrorCode = keyValuePairs<keyType, uint32_t>::BAD_ALLOC;; 
                    }
                }

                __persistent_key_value_pairs_h_debug__ ("Update: OK.");
                xSemaphoreGiveRecursive (__semaphore__); 
                return OK;
            }


           /*
            *  Deletes key-value pair, returns OK or one of the error codes.
            */

            errorCode Delete (keyType key) {
                if (!__dataFile__) { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }

                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!key) {                                                                                               // ... check if parameter construction is valid
                        __persistent_key_value_pairs_h_debug__ ("Delete: key constructor failed.");
                        { lastErrorCode = BAD_ALLOC; return BAD_ALLOC; }
                    }

                xSemaphoreTakeRecursive (__semaphore__, portMAX_DELAY); // don't allow other tasks to change keyValuePairs structure or write to __dataFile__ meanwhile
                if (__inIteration__) {
                    __persistent_key_value_pairs_h_debug__ ("Delete: can not delete while iterating.");
                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = NOT_WHILE_ITERATING; return NOT_WHILE_ITERATING; }
                }

                // 1. get blockOffset
                uint32_t blockOffset;
                if (FindBlockOffset (key, blockOffset) != OK) {
                    __persistent_key_value_pairs_h_debug__ ("Delete: FindBlockOffset failed (1).");
                    errorCode e = lastErrorCode;
                    xSemaphoreGiveRecursive (__semaphore__);
                    return e;
                }

                // 2. read the block size
                if (!__dataFile__.seek (blockOffset, SeekSet)) {
                    __persistent_key_value_pairs_h_debug__ ("Delete: seek failed (2).");
                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                }
                int16_t blockSize;
                if (__dataFile__.read ((uint8_t *) &blockSize, sizeof (int16_t)) != sizeof (blockSize)) {
                    __persistent_key_value_pairs_h_debug__ ("Delete: read failed (2).");
                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                }
                if (blockSize < 0) { 
                    __persistent_key_value_pairs_h_debug__ ("Delete: the block is already free (2).");
                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = DATA_CHANGED; return DATA_CHANGED; } // shouldn't happen, but check anyway ...
                }

                // 3. erase the key from keyValuePairs
                if (keyValuePairs<keyType, uint32_t>::erase (key) != keyValuePairs<keyType, uint32_t>::OK) {
                    __persistent_key_value_pairs_h_debug__ ("Delete: erase failed (3).");
                    errorCode e = lastErrorCode = (errorCode) keyValuePairs<keyType, uint32_t>::lastErrorCode;
                    xSemaphoreGiveRecursive (__semaphore__);
                    return e;
                }                

                // 4. write back negative block size designatin a free block
                blockSize = (int16_t) -blockSize;
                if (!__dataFile__.seek (blockOffset, SeekSet)) {
                    __persistent_key_value_pairs_h_debug__ ("Delete: seek failed (4).");
                    // 5. (try to) roll-back
                    if (keyValuePairs<keyType, uint32_t>::insert (key, (uint32_t) blockOffset) != keyValuePairs<keyType, uint32_t>::OK) {
                        __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                    }
                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                }
                if (__dataFile__.write ((byte *) &blockSize, sizeof (blockSize)) != sizeof (blockSize)) {
                    __persistent_key_value_pairs_h_debug__ ("Delete: write failed (4).");
                     // 5. (try to) roll-back
                    if (keyValuePairs<keyType, uint32_t>::insert (key, (uint32_t) blockOffset) != keyValuePairs<keyType, uint32_t>::OK) {
                        __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                    }
                    xSemaphoreGiveRecursive (__semaphore__);
                    { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                }

                // 5. add the block to __freeBlockList__
                blockSize = (int16_t) -blockSize;
                if (__freeBlocksList__.push_back ( {(uint32_t) blockOffset, blockSize} ) != __freeBlocksList__.OK) {
                    __persistent_key_value_pairs_h_debug__ ("Delete: push_back failed (5).");
                    // it is not really important to return with an error here, persistentKeyValuePairs can continue working with this error 
                    // xSemaphoreGiveRecursive (__semaphore__); 
                    // return keyValuePairs<keyType, uint32_t>::lastErrorCode = keyValuePairs<keyType, uint32_t>::BAD_ALLOC;; 
                }

                __persistent_key_value_pairs_h_debug__ ("Delete: OK.");
                xSemaphoreGiveRecursive (__semaphore__); 
                return OK;
            }


           /*
            *  Truncates key-value pairs, returns OK or one of the error codes.
            */

            errorCode Truncate () {
                xSemaphoreTakeRecursive (__semaphore__, portMAX_DELAY); // don't allow other tasks to change keyValuePairs structure or write to __dataFile__ meanwhile
                    if (__inIteration__) {
                        __persistent_key_value_pairs_h_debug__ ("Truncate: can not truncate while iterating.");
                        xSemaphoreGiveRecursive (__semaphore__);
                        { lastErrorCode = NOT_WHILE_ITERATING; return NOT_WHILE_ITERATING; }
                    }

                    if (__dataFile__) __dataFile__.close (); 

                    __dataFile__ = fileSystem.open (__dataFileName__, "w", true);
                    if (__dataFile__) {
                        __dataFile__.close (); 
                    } else {
                        __persistent_key_value_pairs_h_debug__ ("Truncate: failed creating the data file.");
                        xSemaphoreGiveRecursive (__semaphore__); 
                        { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                    }

                    __dataFile__ = fileSystem.open (__dataFileName__, "r+", false);
                    if (!__dataFile__) {
                        __persistent_key_value_pairs_h_debug__ ("constructor: failed opening the data file.");
                        xSemaphoreGiveRecursive (__semaphore__); 
                        { lastErrorCode = FILE_IO_ERROR; return FILE_IO_ERROR; }
                    }

                    __dataFileSize__ = 0; 
                    keyValuePairs<keyType, uint32_t>::clear ();
                    __freeBlocksList__.clear ();

                xSemaphoreGiveRecursive (__semaphore__); 
                return OK;
            }


           /*
            *  The following iterator overloading is needed so that the calling program can iterate with key-blockOffset pair instead of key-value (value holding the blockOffset) pair.
            *  
            *  Unfortunately iterator's * oprator returns a pointer reather than the reference so the calling program should use "->key" instead of ".key" when
            *  refering to key (and blockOffset):
            *
            *      for (auto p: pkvpA) {
            *          // keys are always kept in memory and are obtained fast
            *          Serial.print (p->key); Serial.print (", "); Serial.print (p->blockOffset); Serial.print (" -> "); 
            *          
            *          // values are read from disk, obtaining a value may be much slower
            *          String value;
            *          persistentKeyValuePairs<int, String>::errorCode e = pkvpA.FindValue (p->key, &value, p->blockOffset);
            *          if (e == pkvpA.OK) 
            *              Serial.println (value);
            *          else
            *              Serial.printf ("Error: %s\n", pkvpA.errorCodeText (e));
            *      }
            *
            * if anyone knows hot to return a reference instead of a pointer a feedback is welcome.
            */

            struct keyBlockOffsetPair {
                keyType key;          // node key
                uint32_t blockOffset; // __dataFile__offset of block containint key-value pair
            };        

            class Iterator : public keyValuePairs<keyType, uint32_t>::Iterator {
                public:
            
                    Iterator (persistentKeyValuePairs* pkvp, int8_t stackSize) : keyValuePairs<keyType, uint32_t>::Iterator (pkvp, stackSize) {
                        __pkvp__ = pkvp;
                        if (__pkvp__) {
                            xSemaphoreTakeRecursive (__pkvp__->__semaphore__, portMAX_DELAY); // iterate in single-task mode (ony with begin instance)
                            __pkvp__->__inIteration__ ++;
                        }
                    }

                    ~Iterator () {
                        if (__pkvp__) {
                            __pkvp__->__inIteration__ --;
                            xSemaphoreGiveRecursive (__pkvp__->__semaphore__); // iterate in single-task mode (ony with begin instance)
                        }
                    }

                    keyBlockOffsetPair * operator * () const { return (keyBlockOffsetPair *) &keyValuePairs<keyType, uint32_t>::Iterator::operator *(); }

                private:
          
                    persistentKeyValuePairs* __pkvp__;

            };      

            Iterator begin () { return Iterator (this, this->height ()); } 
            Iterator end ()   { return Iterator (NULL, 0); } 

            #ifdef __PERSISTENT_KEY_VALUE_PAIRS_H_DEBUG__

                void debugDataFileStructure () {
                    Serial.println ("-----debugDataFileStructure-----");
                    uint64_t blockOffset = 0;

                    while (blockOffset < __dataFileSize__ &&  blockOffset <= 0xFFFFFFFF) { // max uint32_t
                        int16_t blockSize;
                        keyType key;
                        valueType value;

                        Serial.println ("debug [blockOffset = " + String (blockOffset));
                        if (!__readBlock__ (blockSize, key, value, (uint32_t) blockOffset, true)) {
                            Serial.println ("       __readBlock__ failed\n]");
                            return;
                        }
                        if (blockSize > 0) { 
                            Serial.println ("       block is used, blockSize: " + String (blockSize));
                            Serial.println ("       key: " + String (key));
                            Serial.println ("      ]");
                        } else { 
                            Serial.println ("       block is free, blockSize: " + String (blockSize));
                            Serial.println ("      ]");
                            blockSize = (int16_t) -blockSize;
                        } 

                        blockOffset += blockSize;
                    }
                    Serial.println ("=====debugDataFileStructure=====");

                    Serial.println ("-----debugFreeBlockList-----");
                    for (auto block: __freeBlocksList__) 
                        Serial.print (String (block.blockOffset) + " [" + String (block.blockSize) + "]   ");
                    Serial.println ("\n=====debugFreeBlockList=====");

                }

            #endif

        private:

            char __dataFileName__ [FILE_PATH_MAX_LENGTH + 1];
            File __dataFile__;
            uint64_t __dataFileSize__;

            struct freeBlockType {
                uint32_t blockOffset;
                int16_t blockSize;
            };
            vector<freeBlockType> __freeBlocksList__;

            SemaphoreHandle_t __semaphore__ = xSemaphoreCreateRecursiveMutex (); 
            int __inIteration__ = 0;

            
           /*
            *  Reads the value from __dataFile__.
            *  
            *  Returns success, in case of error it also sets lastErrorCode.
            *
            *  This function does not handle the __semaphore__.
            */

            bool __readBlock__ (int16_t& blockSize, keyType& key, valueType& value, uint32_t blockOffset, bool skipReadingValue = false) {
                // reposition file pointer to the beginning of a block
                if (!__dataFile__.seek (blockOffset, SeekSet)) {
                    lastErrorCode = FILE_IO_ERROR;
                    return false;
                }

                // read block size
                if (__dataFile__.read ((uint8_t *) &blockSize, sizeof (int16_t)) != sizeof (blockSize)) {
                    lastErrorCode = FILE_IO_ERROR;
                    return false;
                }
                // if block is free the reading is already done
                if (blockSize < 0) { 
                    return true;
                }

                // read key
                if (std::is_same<keyType, String>::value) { // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    // read the file until 0 is read
                    while (__dataFile__.available ()) { 
                            char c = (char) __dataFile__.read (); 
                            if (!c) break;
                            if (!((String *) &key)->concat (c)) {
                                lastErrorCode = BAD_ALLOC;
                                return false;
                            }
                    }
                } else {
                    // fixed size key
                    if (__dataFile__.read ((uint8_t *) &key, sizeof (key)) != sizeof (key)) {
                        lastErrorCode = FILE_IO_ERROR;
                        return false;
                    }                                
                }

                // read value
                if (!skipReadingValue) {
                    if (std::is_same<valueType, String>::value) { // if value is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                        // read the file until 0 is read
                        while (__dataFile__.available ()) { 
                                char c = (char) __dataFile__.read (); 
                                if (!c) break;
                                if (!((String *) &value)->concat (c)) {
                                    lastErrorCode = BAD_ALLOC;
                                    return false;     
                                }
                        }
                    } else {
                        // fixed size value
                        if (__dataFile__.read ((uint8_t *) &value, sizeof (value)) != sizeof (value)) {
                            lastErrorCode = FILE_IO_ERROR;
                            return false;      
                        }                                
                    }
                }

                return true;
            }

    };

#endif

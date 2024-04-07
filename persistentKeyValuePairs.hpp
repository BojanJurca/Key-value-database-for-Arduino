/*
 * persistentKeyValuePairs.h for Arduino (ESP boards with flash disk)
 * 
 * This file is part of Key-value-database-for-Arduino: https://github.com/BojanJurca/Key-value-database-for-Arduino
 *
 * Key-value-database-for-Arduino may be used as a simple database with the following functions (see examples in BasicUsage.ino).
 * The functions are thread-safe:
 *
 *    - Insert (key, value)                                   - inserts a new key-value pair
 *
 *    - FindBlockOffset (key)                                 - searches (memory) keyValuePairs for key
 *    - FindValue (key, optional block offset)                - searches (memory) keyValuePairs for blockOffset connected to key and then it reads the value from (disk) data file (it works slightly faster if block offset is already known, such as during iterations)
 *
 *    - Update (key, new value, optional block offset)        - updates the value associated by the key (it works slightly faster if block offset is already known, such as during iterations)
 *    - Update (key, callback function, optional blockoffset) - if the calculation is made with existing value then this is prefered method, since calculation is performed while database is being loceks
 *
 *    - Upsert (key, new value)                               - update the value if the key already exists, else insert a new one
 *    - Update (key, callback function, default value)        - if the calculation is made with existing value then this is prefered method, since calculation is performed while database is being loceks
 *
 *    - Delete (key)                                          - deletes key-value pair identified by the key
 *    - Truncate                                              - deletes all key-value pairs
 *
 *    - Iterate                                               - iterate (list) through all the keys and their blockOffsets with an Iterator
 *
 *    - Lock                                                  - locks (takes the semaphore) to (temporary) prevent other taska accessing persistentKeyValuePairs
 *    - Unlock                                                - frees the lock
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
 * Bojan Jurca, March 12, 2024
 *  
 */


#ifndef __PERSISTENT_KEY_VALUE_PAIRS_H__
    #define __PERSISTENT_KEY_VALUE_PAIRS_H__

    // ----- TUNNING PARAMETERS -----

    #define __PERSISTENT_KEY_VALUE_PAIRS_PCT_FREE__ 0.2 // how much space is left free in data block to let data "breed" a little - only makes sense for String values 

    
    #include "keyValuePairs.hpp"
    #include "vector.hpp"


    // #define __PERSISTENT_KEY_VALUE_PAIR_H_EXCEPTIONS__   // uncomment this line if you want keyValuePairs to throw exceptions

    // #define __PERSISTENT_KEY_VALUE_PAIRS_H_DEBUG__       // uncomment this line for debugging puroposes


    // error flags - only tose not defined in keyValuePairs.hpp, please, note that all error flgs are negative (char) numbers
    #define DATA_CHANGED    ((signed char) 0b10010000) // -112 - unexpected data value found
    #define FILE_IO_ERROR   ((signed char) 0b10100000) //  -96 - file operation error
    #define CANT_DO_IT_NOW  ((signed char) 0b11000000) //  -64 - for example changing the data while iterating or loading the data if already loaded


    static SemaphoreHandle_t __persistentKeyValuePairsSemaphore__ = xSemaphoreCreateMutex (); 


    template <class keyType, class valueType> class persistentKeyValuePairs : private keyValuePairs<keyType, uint32_t> {
  
        private: 

            signed char __errorFlags__ = 0;


        public:

            signed char errorFlags () { return __errorFlags__ & 0b01111111; }
            void clearErrorFlags () { __errorFlags__ = 0; }


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
            
            persistentKeyValuePairs () {
                log_i ("persistentKeyValuePairs ()");
            }

           /*
            *  Constructor of persistentKeyValuePairs that also loads the data from data file: 
            *  
            *     persistentKeyValuePairs<int, String> pkvpA ("/persistentKeyValuePairs/A.kvp");
            *     if (pkvpA.lastErrorCode != pkvpA.OK) 
            *         Serial.printf ("pkvpA constructor failed: %s, all the data may not be indexed\n", pkvpA.errorCodeText (pkvpA.lastErrorCode));
            *
            */
            
            persistentKeyValuePairs (const char *dataFileName) { 
                log_i ("persistentKeyValuePairs (dataFileName)");
                loadData (dataFileName);
            }

            ~persistentKeyValuePairs () { 
                log_i ("~persistentKeyValuePairs");
                if (__dataFile__) {
                    __dataFile__.close ();
                }
            } 


           /*
            *  Loads the data from data file.
            *  
            */

            signed char loadData (const char *dataFileName) {
                log_i ("(dataFileName)");
                Lock ();
                if (__dataFile__) {
                    log_e ("data already loaded error: CANT_DO_IT_NOW");
                    __errorFlags__ |= CANT_DO_IT_NOW;
                    Unlock (); 
                    return CANT_DO_IT_NOW;
                }

                // load new data
                strcpy (__dataFileName__, dataFileName);

                if (!fileSystem.isFile (dataFileName)) {
                    log_i ("data file is empty");
                    __dataFile__ = fileSystem.open (dataFileName, "w", true);
                    if (__dataFile__) {
                          __dataFile__.close (); 
                    } else {
                        log_e ("error creating the data file: FILE_IO_ERROR");
                        __errorFlags__ |= FILE_IO_ERROR;
                        Unlock (); 
                        return FILE_IO_ERROR;
                    }
                }

                __dataFile__ = fileSystem.open (dataFileName, "r+", false);
                if (!__dataFile__) {
                    log_e ("error opening the data file: FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    Unlock (); 
                    return FILE_IO_ERROR;
                }

                __dataFileSize__ = __dataFile__.size ();         
                uint64_t blockOffset = 0;

                while (blockOffset < __dataFileSize__ &&  blockOffset <= 0xFFFFFFFF) { // max uint32_t
                    int16_t blockSize;
                    keyType key;
                    valueType value;

                    signed char e = __readBlock__ (blockSize, key, value, (uint32_t) blockOffset, true);
                    if (e) { // != OK
                        log_e ("error reading the data block: FILE_IO_ERROR");
                        __dataFile__.close ();
                        Unlock (); 
                        return e;
                    }
                    if (blockSize > 0) { // block containining the data -> insert into keyValuePairs
                        signed char e = keyValuePairs<keyType, uint32_t>::insert (key, (uint32_t) blockOffset);
                        if (e) { // != OK
                            log_e ("keyValuePairs.insert failed failed");
                            __dataFile__.close ();
                            __errorFlags__ |= keyValuePairs<keyType, uint32_t>::errorFlags ();
                            Unlock (); 
                            return e;
                        }
                    } else { // free block -> insert into __freeBlockList__
                        blockSize = (int16_t) -blockSize;
                        signed char e = __freeBlocksList__.push_back ( {(uint32_t) blockOffset, blockSize} );
                        if (e) { // != OK
                            log_e ("freeeBlockList.push_back failed failed");
                            __dataFile__.close ();
                            __errorFlags__ |= __freeBlocksList__.errorFlags ();
                            Unlock (); 
                            return e;
                        }
                    } 

                    blockOffset += blockSize;
                }

                Unlock (); 
                log_i ("OK");
                return OK;
            }


           /*
            *  Returns true if the data has already been successfully loaded from data file.
            *  
            */

            bool dataLoaded () {
                return __dataFile__;
            }


           /*
            * Returns the lenght of a data file.
            */

            unsigned long dataFileSize () { return __dataFileSize__; } 


           /*
            * Returns the number of key-value pairs.
            */

            int size () { return keyValuePairs<keyType, uint32_t>::size (); }


           /*
            *  Inserts a new key-value pair, returns OK or one of the error codes.
            */

            signed char Insert (keyType key, valueType value) {
                log_i ("(key, value)");
                if (!__dataFile__) { 
                    log_e ("error, data file not opened: FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    return FILE_IO_ERROR; 
                }

                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!(String *) &key) {                                                                                   // ... check if parameter construction is valid
                        log_e ("String key construction error: BAD_ALLOC");
                        __errorFlags__ |= BAD_ALLOC;
                        return BAD_ALLOC;
                    }

                if (std::is_same<valueType, String>::value)                                                                   // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!(String *) &value) {                                                                                 // ... check if parameter construction is valid
                        log_e ("String value construction error: BAD_ALLOC");
                        __errorFlags__ |= BAD_ALLOC;
                        return BAD_ALLOC;
                    }

                Lock (); 
                if (__inIteration__) {
                    log_e ("not while iterating, error: CANT_DO_IT_NOW");
                    __errorFlags__ |= CANT_DO_IT_NOW;
                    Unlock (); 
                    return CANT_DO_IT_NOW;
                }

                // 1. get ready for writting into __dataFile__
                log_i ("step 1: calculate block size");
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
                    log_e ("block size > 32768, error: BAD_ALLOC");
                    __errorFlags__ |= BAD_ALLOC;
                    Unlock (); 
                    return BAD_ALLOC;
                }

                // 2. search __freeBlocksList__ for most suitable free block, if it exists
                log_i ("step 2: find most suitable free block if it already exists");
                int freeBlockIndex = -1;
                uint32_t minWaste = 0xFFFFFFFF;
                for (int i = 0; i < __freeBlocksList__.size (); i ++) {
                    if (__freeBlocksList__ [i].blockSize >= dataSize && __freeBlocksList__ [i].blockSize - dataSize < minWaste) {
                        freeBlockIndex = i;
                        minWaste = __freeBlocksList__ [i].blockSize - dataSize;
                    }
                }

                // 3. reposition __dataFile__ pointer
                log_i ("step 3: reposition data file pointer");
                uint32_t blockOffset;                
                if (freeBlockIndex == -1) { // append data to the end of __dataFile__
                    log_i ("step 3a: appending new block at the end of data file");
                    blockOffset = __dataFileSize__;
                } else { // writte data to free block in __dataFile__
                    log_i ("step 3b: writing new data to exiisting free block");
                    blockOffset = __freeBlocksList__ [freeBlockIndex].blockOffset;
                    blockSize = __freeBlocksList__ [freeBlockIndex].blockSize;
                }
                if (!__dataFile__.seek (blockOffset, SeekSet)) {
                    log_e ("seek error FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    Unlock (); 
                    return FILE_IO_ERROR;
                }

                // 4. update (memory) keyValuePairs structure 
                log_i ("step 4: insert (key, blockOffset) into keyValuePairs");
                signed char e = keyValuePairs<keyType, uint32_t>::insert (key, blockOffset);
                if (e) { // != OK
                    log_e ("keyValuePairs.insert failed failed");
                    __errorFlags__ |= e;
                    Unlock (); 
                    return e;
                }

                // 5. construct the block to be written
                log_i ("step 5: construct data block");
                byte *block = (byte *) malloc (blockSize);
                if (!block) {
                    log_e ("malloc error, out of memory");

                    // 7. (try to) roll-back
                    log_i ("step 7: try to roll-back");
                    signed char e = keyValuePairs<keyType, uint32_t>::erase (key);
                    if (e) { // != OK
                        log_e ("keyValuePairs.erase failed failed, can't roll-back, critical error, closing data file");
                        __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                        __errorFlags__ |= e;
                        Unlock (); 
                        return e;
                    }
                    // roll-back succeded
                    log_e ("roll-back succeeded, returning error: BAD_ALLOC");
                    __errorFlags__ |= BAD_ALLOC;
                    Unlock (); 
                    return BAD_ALLOC;
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
                log_i ("step 6: write block to data file");
                if (__dataFile__.write (block, blockSize) != blockSize) {
                    log_e ("write failed");
                    free (block);

                    // 9. (try to) roll-back
                    log_i ("step 9: try to roll-back");
                    if (__dataFile__.seek (blockOffset, SeekSet)) {
                        blockSize = (int16_t) -blockSize;
                        if (__dataFile__.write ((byte *) &blockSize, sizeof (blockSize)) != sizeof (blockSize)) { // can't roll-back
                            log_e ("write error, can't roll-back, critical error, closing data file");
                            __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                        }
                    } else { // can't roll-back
                        log_e ("seek error, can't roll-back, critical error, closing data file");
                        __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                    }
                    __dataFile__.flush ();

                    signed char e = keyValuePairs<keyType, uint32_t>::erase (key);
                    if (e) { // != OK
                        log_e ("keyValuePairs.erase failed failed, can't roll-back, critical error, closing data file");
                        __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                        __errorFlags__ |= keyValuePairs<keyType, uint32_t>::errorFlags ();
                        Unlock (); 
                        return e;
                    }
                    // roll-back succeded
                    log_e ("roll-back succeeded, returning error: FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    Unlock (); 
                    return FILE_IO_ERROR;
                }

                // write succeeded
                __dataFile__.flush ();
                free (block);

                // 8. roll-out
                // __persistent_key_value_pairs_h_debug__ ("Insert: step 8 - roll-out");
                if (freeBlockIndex == -1) { // data appended to the end of __dataFile__
                    __dataFileSize__ += blockSize;       
                } else { // data written to free block in __dataFile__
                    __freeBlocksList__.erase (freeBlockIndex); // doesn't fail
                }
                
                log_i ("OK");
                Unlock (); 
                return OK;
            }


           /*
            *  Retrieve blockOffset from (memory) keyValuePairs, so it is fast.
            */

            signed char FindBlockOffset (keyType key, uint32_t& blockOffset) {
                log_i ("(key, block offset)");
                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!(String *) &key) {                                                                                   // ... check if parameter construction is valid
                        log_e ("String key construction error: BAD_ALLOC");
                        __errorFlags__ |= BAD_ALLOC;
                        return BAD_ALLOC;
                    }

                Lock ();
                keyValuePairs<keyType, uint32_t>::clearErrorFlags ();
                uint32_t *p = keyValuePairs<keyType, uint32_t>::find (key);
                if (p) { // if found
                    blockOffset = *p;
                    Unlock ();  
                    log_i ("OK");
                    return OK;
                } else { // not found or error
                    signed char e = keyValuePairs<keyType, uint32_t>::errorFlags ();
                    if (e == NOT_FOUND) {
                        log_i ("error (key): NOT_FOUD");
                        __errorFlags__ |= NOT_FOUND;
                        Unlock ();  
                        return NOT_FOUND;
                    } else {
                        log_i ("error: some other error, check result");
                        __errorFlags__ |= e;
                        Unlock ();  
                        return e;
                    }
                }
            }


           /*
            *  Read the value from (disk) __dataFile__, so it is slow. 
            */

            signed char FindValue (keyType key, valueType *value, uint32_t blockOffset = 0xFFFFFFFF) { 
                log_i ("(key, *value, block offset)");
                if (!__dataFile__) { 
                    log_e ("error, data file not opened: FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    return FILE_IO_ERROR; 
                }

                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!(String *) &key) {                                                                                   // ... check if parameter construction is valid
                        log_e ("String key construction error: BAD_ALLOC");
                        __errorFlags__ |= BAD_ALLOC;
                        return BAD_ALLOC;
                    }

                keyType storedKey = {};

                Lock (); 

                if (blockOffset == 0xFFFFFFFF) { // if block offset was not specified find it from keyValuePairs
                    keyValuePairs<keyType, uint32_t>::clearErrorFlags ();
                    uint32_t *pBlockOffset = keyValuePairs<keyType, uint32_t>::find (key);
                    if (!pBlockOffset) { // if not found or error
                        signed char e = keyValuePairs<keyType, uint32_t>::errorFlags ();
                        if (e) { // != OK
                            log_i ("error (key): NOT_FOUD");
                            __errorFlags__ |= NOT_FOUND;
                            Unlock ();  
                            return NOT_FOUND;
                        } else {
                            log_i ("error: some other error, check result");
                            __errorFlags__ |= e;
                            Unlock ();  
                            return e;
                        }
                    }
                    blockOffset = *pBlockOffset;
                }

                int16_t blockSize;
                if (!__readBlock__ (blockSize, storedKey, *value, blockOffset)) {
                    if (blockSize > 0 && storedKey == key) {
                        log_i ("OK");
                        Unlock ();  
                        return OK; // success  
                    } else {
                        log_e ("error that shouldn't happen: DATA_CHANGED");
                        __errorFlags__ |= DATA_CHANGED;
                        Unlock ();  
                        return DATA_CHANGED; // shouldn't happen, but check anyway ...
                    }
                } else {
                    log_e ("error reading data block: FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    Unlock ();  
                    return FILE_IO_ERROR; 
                } 
            }


           /*
            *  Updates the value associated with the key
            */

            signed char Update (keyType key, valueType newValue, uint32_t *pBlockOffset = NULL) {
                log_i ("(key, value)");
                if (!__dataFile__) { 
                    log_e ("error, data file not opened: FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    return FILE_IO_ERROR; 
                }

                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!(String *) &key) {                                                                                   // ... check if parameter construction is valid
                        log_e ("String key construction error: BAD_ALLOC");
                        __errorFlags__ |= BAD_ALLOC;
                        return BAD_ALLOC;
                    }

                if (std::is_same<valueType, String>::value)                                                                   // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!(String *) &newValue) {                                                                                 // ... check if parameter construction is valid
                        log_e ("String value construction error: BAD_ALLOC");
                        __errorFlags__ |= BAD_ALLOC;
                        return BAD_ALLOC;
                    }

                Lock (); 

                // 1. get blockOffset
                if (!pBlockOffset) { // fing block offset if not provided by the calling program
                    log_i ("step 1: looking for block offset in keyValuePairs");
                    keyValuePairs<keyType, uint32_t>::clearErrorFlags ();
                    pBlockOffset = keyValuePairs<keyType, uint32_t>::find (key);
                    if (!pBlockOffset) { // if not found
                        if (keyValuePairs<keyType, uint32_t>::errorFlags ()) {
                            log_e ("block offset not found for given key: NOT_FOUND");
                            __errorFlags__ |= NOT_FOUND;
                            Unlock ();  
                            return NOT_FOUND;
                        } else {
                            log_e ("block offset not found for some kind of error occured");
                            signed char e = keyValuePairs<keyType, uint32_t>::errorFlags ();
                            __errorFlags__ |= e;
                            Unlock ();  
                            return e;
                        }
                    }
                } else {
                    log_i ("step 1: block offset already proficed by the calling program");
                }

                // 2. read the block size and stored key
                log_i ("step 2: reading block size from data file");
                int16_t blockSize;
                size_t newBlockSize;
                keyType storedKey;
                valueType storedValue;

                signed char e = __readBlock__ (blockSize, storedKey, storedValue, *pBlockOffset, true);
                if (e) { // != OK
                    log_e ("read block error");
                    Unlock ();  
                    return FILE_IO_ERROR;
                } 
                if (blockSize <= 0 || storedKey != key) {
                    log_e ("error that shouldn't happen: DATA_CHANGED");
                    __errorFlags__ |= DATA_CHANGED;
                    Unlock ();  
                    return DATA_CHANGED; // shouldn't happen, but check anyway ...
                }

                // 3. calculate new block and data size
                log_i ("step 3: calculate block size");
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
                    // __persistent_key_value_pairs_h_debug__ ("Update: calculated (disk) block size is too large.");
                    log_e ("block size > 32768, error: BAD_ALLOC");
                    __errorFlags__ |= BAD_ALLOC;
                    Unlock (); 
                    return BAD_ALLOC;
                }

                // 4. decide where to write the new value: existing block or a new one
                log_i ("step 4: decide where to writte the new value: same or new block?");
                if (dataSize <= blockSize) { // there is enough space for new data in the existing block - easier case
                    log_i ("reuse the same block");
                    uint32_t dataFileOffset = *pBlockOffset + sizeof (int16_t); // skip block size information
                    if (std::is_same<keyType, String>::value) { // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                        dataFileOffset += (((String *) &key)->length () + 1); // add 1 for closing 0
                    } else { // fixed size key
                        dataFileOffset += sizeof (keyType);
                    }                

                    // 5. write new value to __dataFile__
                    log_i ("step 5: write new value");
                    if (!__dataFile__.seek (dataFileOffset, SeekSet)) {
                        log_e ("seek error: FILE_IO_ERROR");
                        __errorFlags__ |= FILE_IO_ERROR;
                        Unlock ();  
                        return FILE_IO_ERROR;
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
                        log_e ("write failed failed, can't roll-back, critical error, closing data file");
                        __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                        __errorFlags__ |= FILE_IO_ERROR;
                        Unlock ();  
                        return FILE_IO_ERROR;
                    }
                    // success
                    __dataFile__.flush ();
                    Unlock ();  
                    log_i ("OK");
                    return OK;

                } else { // existing block is not big eneugh, we'll need a new block - more difficult case
                    // __persistent_key_value_pairs_h_debug__ ("Update: writing changed data to new (disk) block");
                    log_i ("new block is needed");

                    // 6. search __freeBlocksList__ for most suitable free block, if it exists
                    log_i ("step 6: searching for the best block on free block list");
                    int freeBlockIndex = -1;
                    uint32_t minWaste = 0xFFFFFFFF;
                    for (int i = 0; i < __freeBlocksList__.size (); i ++) {
                        if (__freeBlocksList__ [i].blockSize >= dataSize && __freeBlocksList__ [i].blockSize - dataSize < minWaste) {
                            freeBlockIndex = i;
                            minWaste = __freeBlocksList__ [i].blockSize - dataSize;
                        }
                    }

                    // 7. reposition __dataFile__ pointer
                    log_i ("step 7: reposition data file pointer");
                    uint32_t newBlockOffset;          
                    if (freeBlockIndex == -1) { // append data to the end of __dataFile__
                        log_i ("append data to the end of data file");
                        newBlockOffset = __dataFileSize__;
                    } else { // writte data to free block in __dataFile__
                        log_i ("found suitabel free data block");
                        newBlockOffset = __freeBlocksList__ [freeBlockIndex].blockOffset;
                        newBlockSize = __freeBlocksList__ [freeBlockIndex].blockSize;
                    }
                    if (!__dataFile__.seek (newBlockOffset, SeekSet)) {
                        log_e ("seek error FILE_IO_ERROR");
                        __errorFlags__ |= FILE_IO_ERROR;
                        Unlock (); 
                        return FILE_IO_ERROR;
                    }

                    // 8. construct the block to be written
                    log_i ("step 8: construct data block");
                    byte *block = (byte *) malloc (newBlockSize);
                    if (!block) {
                        log_e ("malloc error, out of memory");
                        __errorFlags__ |= BAD_ALLOC;
                        Unlock (); 
                        return BAD_ALLOC;
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
                    log_i ("step 9: write new block to data file");
                    if (__dataFile__.write (block, dataSize) != dataSize) {
                        log_e ("write failed");
                        free (block);

                        // 10. (try to) roll-back
                        log_i ("step 10: try to roll-back");
                        if (__dataFile__.seek (newBlockOffset, SeekSet)) {
                            newBlockSize = (int16_t) -newBlockSize;
                            if (__dataFile__.write ((byte *) &newBlockSize, sizeof (newBlockSize)) != sizeof (newBlockSize)) { // can't roll-back
                                __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                            }
                        } else { // can't roll-back 
                            log_e ("seek failed failed, can't roll-back, critical error, closing data file");
                            __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                        }
                        log_e ("error FILE_IO_ERROR");
                        __errorFlags__ |= FILE_IO_ERROR;
                        Unlock (); 
                        return FILE_IO_ERROR;
                    }
                    free (block);
                    __dataFile__.flush ();

                    // 11. roll-out
                    log_i ("step 11: roll-out");
                    if (freeBlockIndex == -1) { // data appended to the end of __dataFile__
                        __dataFileSize__ += newBlockSize;
                    } else { // data written to free block in __dataFile__
                        __freeBlocksList__.erase (freeBlockIndex); // doesn't fail
                    }
                    // mark old block as free
                    if (!__dataFile__.seek (*pBlockOffset, SeekSet)) {
                        log_e ("seek error: FILE_IO_ERROR");
                        __dataFile__.close (); // data file is corrupt (it contains two entries with the same key) and it is not likely we can roll it back
                        __errorFlags__ |= FILE_IO_ERROR;
                        Unlock (); 
                        return FILE_IO_ERROR;
                    }
                    blockSize = (int16_t) -blockSize;
                    if (__dataFile__.write ((byte *) &blockSize, sizeof (blockSize)) != sizeof (blockSize)) {
                        log_e ("write error: FILE_IO_ERROR");
                        __dataFile__.close (); // data file is corrupt (it contains two entries with the same key) and it si not likely we can roll it back
                        __errorFlags__ |= FILE_IO_ERROR;
                        Unlock (); 
                        return FILE_IO_ERROR;
                    }
                    __dataFile__.flush ();
                    // update __freeBlocklist__
                    // __persistent_key_value_pairs_h_debug__ ("Update roll-out: add block to free list ( offset , size ) = ( " + String (*pBlockOffset) + " , " + String (-blockSize) + " )");
                    if (__freeBlocksList__.push_back ( {*pBlockOffset, (int16_t) -blockSize} )) { // != OK
                        log_i ("free block list push_back failed, continuing anyway");
                    }
                    // update keyValuePairs information
                    *pBlockOffset = newBlockOffset; // there is no reason this would fail
                    Unlock ();  
                    log_i ("OK");
                    return OK;
                }

                // Unlock ();  
                // return OK;
            }


           /*
            *  Updates the value associated with the key throught callback function (usefull for counting, etc, when all the calculation should be done while locking is in place)
            */

            signed char Update (keyType key, void (*updateCallback) (valueType &value), uint32_t *pBlockOffset = NULL) {
                log_i ("(key, value)");
                if (!__dataFile__) { 
                    log_e ("error, data file not opened: FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    return FILE_IO_ERROR; 
                }

                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!(String *) &key) {                                                                                   // ... check if parameter construction is valid
                        log_e ("String key construction error: BAD_ALLOC");
                        __errorFlags__ |= BAD_ALLOC;
                        return BAD_ALLOC;
                    }

                Lock (); 

                valueType value;
                signed char e = FindValue (key, &value); 
                if (e) {
                    log_e ("FindValue error");
                    __errorFlags__ |= e;
                    Unlock ();  
                    return e;
                }

                updateCallback (value);

                e = Update (key, value, pBlockOffset); 
                if (e) {
                    log_e ("Update error");
                    __errorFlags__ |= e;
                    Unlock ();  
                    return e;
                }                
                log_i ("OK");
                Unlock ();
                return OK;
            }


           /*
            *  Updates or inserts key-value pair
            */

            signed char Upsert (keyType key, valueType newValue) {
                log_i ("(key, value)");
                if (!__dataFile__) { 
                    log_e ("error, data file not opened: FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    return FILE_IO_ERROR; 
                }

                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!(String *) &key) {                                                                                   // ... check if parameter construction is valid
                        log_e ("String key construction error: BAD_ALLOC");
                        __errorFlags__ |= BAD_ALLOC;
                        return BAD_ALLOC;
                    }

                if (std::is_same<valueType, String>::value)                                                                   // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!(String *) &newValue) {                                                                                 // ... check if parameter construction is valid
                        log_e ("String value construction error: BAD_ALLOC");
                        __errorFlags__ |= BAD_ALLOC;
                        return BAD_ALLOC;
                    }

                Lock (); 
                signed char e = Update (key, newValue);
                if (e == NOT_FOUND) 
                    e = Insert (key, newValue);
                if (e) {
                    log_e ("Update or Insert error");
                  __errorFlags__ |= e;
                } else {
                    log_i ("OK");
                }
                Unlock ();
                return e; 
            }

           /*
            *  Updates or inserts the value associated with the key throught callback function (usefull for counting, etc, when all the calculation should be done while locking is in place)
            */

            signed char Upsert (keyType key, void (*updateCallback) (valueType &value), valueType defaultValue) {
                log_i ("(key, value)");
                if (!__dataFile__) { 
                    log_e ("error, data file not opened: FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    return FILE_IO_ERROR; 
                }

                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!(String *) &key) {                                                                                   // ... check if parameter construction is valid
                        log_e ("String key construction error: BAD_ALLOC");
                        __errorFlags__ |= BAD_ALLOC;
                        return BAD_ALLOC;
                    }

                if (std::is_same<valueType, String>::value)                                                                   // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!(String *) &defaultValue) {                                                                                 // ... check if parameter construction is valid
                        log_e ("String value construction error: BAD_ALLOC");
                        __errorFlags__ |= BAD_ALLOC;
                        return BAD_ALLOC;
                    }

                Lock (); 
                signed char e = Update (key, updateCallback);
                if (e == NOT_FOUND) 
                    e = Insert (key, defaultValue);
                if (e) {
                    log_e ("Update or Insert error");
                  __errorFlags__ |= e;
                } else {
                    log_i ("OK");
                }
                Unlock ();
                return e; 
            }


           /*
            *  Deletes key-value pair, returns OK or one of the error codes.
            */

            signed char Delete (keyType key) {
                log_i ("(key, value)");
                if (!__dataFile__) { 
                    log_e ("error, data file not opened: FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    return FILE_IO_ERROR; 
                }

                if (std::is_same<keyType, String>::value)                                                                     // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    if (!(String *) &key) {                                                                                   // ... check if parameter construction is valid
                        log_e ("String key construction error: BAD_ALLOC");
                        __errorFlags__ |= BAD_ALLOC;
                        return BAD_ALLOC;
                    }

                Lock (); 

                if (__inIteration__) {
                    log_e ("not while iterating, error: CANT_DO_IT_NOW");                  
                    __errorFlags__ |= CANT_DO_IT_NOW;
                    Unlock (); 
                    return CANT_DO_IT_NOW;
                }

                // 1. get blockOffset
                log_i ("step 1: get block offset");
                uint32_t blockOffset;
                signed char e = FindBlockOffset (key, blockOffset);
                if (e) { // != OK
                    log_e ("FindBlockOffset failed");
                    Unlock (); 
                    return e;
                }

                // 2. read the block size
                log_i ("step 2: reading block size from data file");
                // __persistent_key_value_pairs_h_debug__ ("DEELTE (step 2): read block size");
                if (!__dataFile__.seek (blockOffset, SeekSet)) {
                    log_e ("seek failed, error FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    Unlock (); 
                    return FILE_IO_ERROR;
                }
                int16_t blockSize;
                if (__dataFile__.read ((uint8_t *) &blockSize, sizeof (int16_t)) != sizeof (blockSize)) {
                    log_e ("read failed, error FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    Unlock (); 
                    return FILE_IO_ERROR;
                }
                if (blockSize < 0) { 
                    log_e ("error that shouldn't happen: DATA_CHANGED");
                    __errorFlags__ |= DATA_CHANGED;
                    Unlock (); 
                    return DATA_CHANGED; // shouldn't happen, but check anyway ...
                }

                // 3. erase the key from keyValuePairs
                log_i ("step 3: erase key from keyValuePairs");
                e = keyValuePairs<keyType, uint32_t>::erase (key);
                if (e) { // != OK
                    //__persistent_key_value_pairs_h_debug__ ("Delete: erase failed (3).");
                    log_e ("keyValuePairs::erase failed");
                    __errorFlags__ |= e;
                    Unlock (); 
                    return e;
                }

                // 4. write back negative block size designatin a free block
                log_i ("step 4: mark bloc as free");
                blockSize = (int16_t) -blockSize;
                if (!__dataFile__.seek (blockOffset, SeekSet)) {
                  log_e ("seek failed, error FILE_IO_ERROR");

                    // 5. (try to) roll-back
                    log_i ("step 5: try to roll-back");
                    if (keyValuePairs<keyType, uint32_t>::insert (key, (uint32_t) blockOffset)) { // != OK
                        log_e ("keyValuePairs::insert failed failed, can't roll-back, critical error, closing data file");
                        __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost the file, this would cause all disk related operations from now on to fail
                    }
                    log_e ("error FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    Unlock (); 
                    return FILE_IO_ERROR;
                }
                if (__dataFile__.write ((byte *) &blockSize, sizeof (blockSize)) != sizeof (blockSize)) {
                    log_e ("write failed, try to roll-back");
                     // 5. (try to) roll-back
                    if (keyValuePairs<keyType, uint32_t>::insert (key, (uint32_t) blockOffset)) { // != OK
                        log_e ("keyValuePairs::insert failed failed, can't roll-back, critical error, closing data file");
                        __dataFile__.close (); // memory key value pairs and disk data file are synchronized any more - it is better to clost he file, this would cause all disk related operations from now on to fail
                    }
                    log_e ("error FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    Unlock (); 
                    return FILE_IO_ERROR;
                }
                __dataFile__.flush ();

                // 5. roll-out
                log_i ("step 5: roll-out");
                // add the block to __freeBlockList__
                blockSize = (int16_t) -blockSize;
                if (__freeBlocksList__.push_back ( {(uint32_t) blockOffset, blockSize} )) { // != OK
                    log_i ("free block list push_back failed, continuing anyway");
                    // it is not really important to return with an error here, persistentKeyValuePairs can continue working with this error
                }
                log_i ("OK");
                Unlock ();  
                return OK;
            }


           /*
            *  Truncates key-value pairs, returns OK or one of the error codes.
            */

            signed char Truncate () {
                log_i ("()");
                
                Lock (); 
                    if (__inIteration__) {
                      log_e ("not while iterating, error: CANT_DO_IT_NOW");
                        __errorFlags__ |= CANT_DO_IT_NOW;
                        Unlock (); 
                        return CANT_DO_IT_NOW;
                    }

                    if (__dataFile__) __dataFile__.close (); 

                    __dataFile__ = fileSystem.open (__dataFileName__, "w", true);
                    if (__dataFile__) {
                        __dataFile__.close (); 
                    } else {
                        log_e ("truncate failed, error FILE_IO_ERROR");
                        __errorFlags__ |= FILE_IO_ERROR;
                        Unlock ();  
                        return FILE_IO_ERROR;
                    }

                    __dataFile__ = fileSystem.open (__dataFileName__, "r+", false);
                    if (!__dataFile__) {
                        log_e ("data file open failed, error FILE_IO_ERROR");
                        __errorFlags__ |= FILE_IO_ERROR;
                        Unlock ();  
                        return FILE_IO_ERROR;
                    }

                    __dataFileSize__ = 0; 
                    keyValuePairs<keyType, uint32_t>::clear ();
                    __freeBlocksList__.clear ();
                log_i ("OK");
                Unlock ();  
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
            */

            struct keyBlockOffsetPair {
                keyType key;          // node key
                uint32_t blockOffset; // __dataFile__ offset of block containing a key-value pair
            };        

            class Iterator : public keyValuePairs<keyType, uint32_t>::Iterator {
                public:
            
                    // called form begin () and first_element () - since only the begin () instance is used for iterating we'll do the locking here ...
                    Iterator (persistentKeyValuePairs* pkvp, int8_t stackSize) : keyValuePairs<keyType, uint32_t>::Iterator (pkvp, stackSize) {
                        __pkvp__ = pkvp;
                    }

                    // caled form end () andl last_element () 
                    Iterator (int8_t stackSize, persistentKeyValuePairs* pkvp) : keyValuePairs<keyType, uint32_t>::Iterator (stackSize, pkvp) {
                        __pkvp__ = pkvp;
                    }

                    ~Iterator () {
                        if (__pkvp__) {
                            __pkvp__->__inIteration__ --;
                            __pkvp__->Unlock (); 
                        }
                    }

                    // keyBlockOffsetPair& operator * () const { return (keyBlockOffsetPair&) keyValuePairs<keyType, uint32_t>::Iterator::operator *(); }
                    keyBlockOffsetPair * operator * () const { return (keyBlockOffsetPair *) keyValuePairs<keyType, uint32_t>::Iterator::operator *(); }

                    // this will tell if iterator is valid (if there are not elements the iterator can not be valid)
                    operator bool () const { return __pkvp__->size () > 0; }


                private:
          
                    persistentKeyValuePairs* __pkvp__ = NULL;

            };

            Iterator begin () { // since only the begin () instance is neede for iteration we'll do the locking here
                Lock (); // Unlock () will be called in instance destructor
                __inIteration__ ++; // -- will be called in instance destructor
                return Iterator (this, this->height ()); 
            } 

            Iterator end () { 
                return Iterator ((int8_t) 0, (persistentKeyValuePairs *) NULL); 
            } 


           /*
            *  Finds min and max keys in persistentKeyValuePairs.
            *
            *  Example:
            *  
            *    auto firstElement = pkvpA.first_element ();
            *    if (firstElement) // check if first element is found (if pkvpA is not empty)
            *        Serial.printf ("first element (min key) of pkvpA = %i\n", (*firstElement)->key);
            */

          Iterator first_element () { 
              Lock (); // Unlock () will be called in instance destructor
              __inIteration__ ++; // -- will be called in instance destructor
              return Iterator (this, this->height ());  // call the 'begin' constructor
          }

          Iterator last_element () {
              Lock (); // Unlock () will be called in instance destructor
              __inIteration__ ++; // -- will be called in instance destructor
              return Iterator (this->height (), this);  // call the 'end' constructor
          }


           /*
            * Locking mechanism
            */

            void Lock () { xSemaphoreTakeRecursive (__semaphore__, portMAX_DELAY); } 

            void Unlock () { xSemaphoreGiveRecursive (__semaphore__); }


        private:

            char __dataFileName__ [FILE_PATH_MAX_LENGTH + 1] = "";
            File __dataFile__;
            unsigned long __dataFileSize__ = 0;

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

            signed char __readBlock__ (int16_t& blockSize, keyType& key, valueType& value, uint32_t blockOffset, bool skipReadingValue = false) {
                // reposition file pointer to the beginning of a block
                if (!__dataFile__.seek (blockOffset, SeekSet)) {
                    log_e ("seek error FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;
                    return FILE_IO_ERROR;
                }

                // read block size
                if (__dataFile__.read ((uint8_t *) &blockSize, sizeof (int16_t)) != sizeof (blockSize)) {
                    log_e ("read block size error FILE_IO_ERROR");
                    __errorFlags__ |= FILE_IO_ERROR;                    
                    return FILE_IO_ERROR;
                }
                // if block is free the reading is already done
                if (blockSize < 0) { 
                    log_i ("OK");
                    return OK;
                }

                // read key
                if (std::is_same<keyType, String>::value) { // if key is of type String ... (if anyone knows hot to do this in compile-time a feedback is welcome)
                    // read the file until 0 is read
                    while (__dataFile__.available ()) { 
                            char c = (char) __dataFile__.read (); 
                            if (!c) break;
                            if (!((String *) &key)->concat (c)) {
                                log_e ("String key construction error BAD_ALLOC");
                                __errorFlags__ |= BAD_ALLOC;
                                return BAD_ALLOC;
                            }
                    }
                } else {
                    // fixed size key
                    if (__dataFile__.read ((uint8_t *) &key, sizeof (key)) != sizeof (key)) {
                        log_e ("read key error FILE_IO_ERROR");
                        __errorFlags__ |= FILE_IO_ERROR;
                        return FILE_IO_ERROR;
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
                                    log_e ("String value construction error BAD_ALLOC");
                                    __errorFlags__ |= BAD_ALLOC;
                                    return BAD_ALLOC;     
                                }
                        }
                    } else {
                        // fixed size value
                        if (__dataFile__.read ((uint8_t *) &value, sizeof (value)) != sizeof (value)) {
                            log_e ("read value error FILE_IO_ERROR");
                            __errorFlags__ |= FILE_IO_ERROR;
                            return FILE_IO_ERROR;      
                        }                                
                    }
                }
                log_i ("OK");
                return OK;
            }

    };


    #ifndef __FIRST_LAST_ELEMENT__ 
        #define __FIRST_LAST_ELEMENT__

        template <typename T>
        typename T::Iterator first_element (T& obj) { return obj.first_element (); }

        template <typename T>
        typename T::Iterator last_element (T& obj) { return obj.last_element (); }

    #endif


#endif

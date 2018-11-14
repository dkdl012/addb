/*
 * 2018.3.15
 * hssung@yonsei.ac.kr
 * Add File for relational model in Redis
 */

#include "server.h"
#include "assert.h"
#include "addb_relational.h"
#include "stl.h"
#include "circular_queue.h"

/*ADDB*/
/*fpWrite parameter list
 * arg1 : dataKeyInfo
 * arg2 : partitionInfo
 * arg3 : filter index column
 * arg4 : */

void fpWriteCommand(client *c){

    serverLog(LL_DEBUG,"FPWRITE COMMAND START");

    int fpWrite_result = C_OK;
    int i;
    long long insertedRow = 0;
    int Enroll_queue = 0;

    //struct redisClient *fakeClient = NULL;

    serverLog(LL_DEBUG, "fpWrite Param List ==> Key : %s, partition : %s, num_of_column : %s, indexColumn : %s",
            (char *) c->argv[1]->ptr,(char *) c->argv[2]->ptr, (char *) c->argv[3]->ptr , (char *) c->argv[4]->ptr);

    /*parsing dataInfo*/
    NewDataKeyInfo *dataKeyInfo = parsingDataKeyInfo((sds)c->argv[1]->ptr);

    /*get column number*/
    int column_number = atoi((char *) c->argv[3]->ptr);
    assert(column_number <= MAX_COLUMN_NUMBER);
    serverLog(LL_DEBUG, "fpWrite Column Number : %d", column_number);

    /*get value number*/
    int value_num = c->argc - 5;
    serverLog(LL_DEBUG ,"VALUE NUM : %d", value_num);

    /*compare with column number and arguments*/
    if((value_num % column_number) != 0 ){
    	serverLog(LL_WARNING,"column number and args number do not match");
    	addReplyError(c, "column_number Error");
    	return;
    }

    serverLog(LL_DEBUG,"VALID DATAKEYSTRING ==> tableId : %d, partitionInfo : %s, rowgroup : %d",
              dataKeyInfo->tableId, dataKeyInfo->partitionInfo.partitionString, dataKeyInfo->rowGroupId);

    /*get rowgroup info from Metadict*/
    int rowGroupId = getRowgroupInfo(c->db, dataKeyInfo);
    serverLog(LL_DEBUG, "rowGroupId = %d", rowGroupId);

    /*get rownumber info from Metadict*/
    int row_number = getRowNumberInfoAndSetRowNumberInfo(c->db, dataKeyInfo);
    serverLog(LL_DEBUG, "rowNumber = %d", row_number);
    int prev_row = row_number;

    /*set rowNumber Info to Metadict*/
    if(row_number == 0 ){
    	incRowNumber(c->db, dataKeyInfo, 0);
    }
    /*check rowgroup size*/
    if(row_number >= server.rowgroup_size){
    	rowGroupId = IncRowgroupIdAndModifyInfo(c->db, dataKeyInfo, 1);
    	row_number = 0;
    	Enroll_queue = 1;
    }


		robj * dataKeyString = NULL;
		dataKeyString = generateDataKey(dataKeyInfo);
		//serverLog(LL_VERBOSE, "DATAKEY1 :  %s", (char *) dataKeyString->ptr);

		dictEntry *entryDict = dictFind(c->db->dict, dataKeyString->ptr);
		if (entryDict != NULL) {
			robj * val = dictGetVal(entryDict);
			//serverLog(LL_VERBOSE, "DATAKEYtest :  %d", val->location);

			__sync_synchronize();
			if (val->location != LOCATION_REDIS_ONLY && !Enroll_queue) {
				rowGroupId = IncRowgroupIdAndModifyInfo(c->db, dataKeyInfo, 1);
//		    incRowNumber(c->db, dataKeyInfo, 0);
				decrRefCount(dataKeyString);
				dataKeyString = generateDataKey(dataKeyInfo);
				row_number = 0;
				//serverLog(LL_VERBOSE, "DATAKEY2 :  %s, rowGroupId : %d  rowgroupId : %d",
				//		(char *) dataKeyString->ptr, rowGroupId, dataKeyInfo->rowGroupId);
			}
		}

	  int init = 0;
    int idx = 0;
    for(i = 5; i < c->argc; i++){

    	   /*TODO - pk column check & ROW MAX LIMIT, COLUMN MAX LIMIT, */

    	robj *valueObj = getDecodedObject(c->argv[i]);

    	//Create field Info
    	int row_idx = row_number + (idx / column_number) + 1;
    	int column_idx = (idx % column_number) + 1;
     assert(column_idx <= MAX_COLUMN_NUMBER);

    	robj *dataField = getDataField(row_idx, column_idx);
     serverLog(LL_DEBUG, "DATAFIELD KEY = %s", (char *)dataField->ptr);
     assert(dataField != NULL);


     /*check Value Type*/
     if(!(strcmp((char *)valueObj->ptr, NULLVALUE)))
        	valueObj = shared.nullValue;


     serverLog(LL_DEBUG, "insertKVpairToRelational key : %s, field : %s, value : %s",
        		(char *)dataKeyString->ptr, (char *)dataField->ptr, (char *)valueObj->ptr);

        /*insert data into dict with Relational model*/

     init = insertKVpairToRelational(c, dataKeyString, dataField, valueObj);

			if (init)
				Enroll_queue++;
			idx++;
			insertedRow++;
			decrRefCount(dataField);
			decrRefCount(valueObj);
    }

    /*addb update row number info*/
    insertedRow /= column_number;
    incRowNumber(c->db, dataKeyInfo, insertedRow);

    /*TODO - filter*/
    /*TODO - eviction insert*/

    serverLog(LL_DEBUG,"FPWRITE COMMAND END");

    serverLog(LL_DEBUG,"DictEntry Registration in a circular queue START");

    if(Enroll_queue) {
    	if(enqueue(c->db->EvictQueue, dictFind(c->db->dict, dataKeyString->ptr)) == 0) {
    	 	serverLog(LL_VERBOSE, "Enqueue queue : %d --- prev_row : %d --- String : %s " ,
    	 			Enroll_queue, prev_row, dataKeyString->ptr);
    	 	serverAssert(0);
    	}
    }
//    if(dataKeyInfo->rowGroupId == 1){
//    	int enqueue_row = getRowNumberInfoAndSetRowNumberInfo(c->db, dataKeyInfo);
//
//    	if(enqueue_row == 1){
//    	  int firstrgid_result;
//    	            serverLog(LL_DEBUG,"Find First Rowgroup");
//    	            dictEntry *FirstRowgroupEntry = getCandidatedictEntry(c, dataKeyInfo);
//    	            if((firstrgid_result = enqueue(c->db->EvictQueue, FirstRowgroupEntry)) ==  0){
//    	            	serverLog(LL_WARNING, "Enqueue Fail FirstRowgroup dictEntry");
//    	            	serverAssert(0);
//    	            }
//    	}
//    }
//
//    if(Enroll_queue == 1){
//
//    	int result;
//
//    	//Enroll Another rowgroup
//    	serverLog(LL_DEBUG, "Enqueue Rowgroup");
//    	dictEntry *CandidateRowgroupEntry = getCandidatedictEntry(c, dataKeyInfo);
//    	if((result = enqueue(c->db->EvictQueue, CandidateRowgroupEntry)) ==  0){
//    		serverLog(LL_WARNING, "Enqueue Fail CandidateRowgroup dictEntry");
//    		serverAssert(0);
//    	}
//    }
    decrRefCount(dataKeyString);
    zfree(dataKeyInfo);
    addReply(c, shared.ok);
}


void fpReadCommand(client *c) {
    serverLog(LL_DEBUG,"FPREAD COMMAND START");
    getGenericCommand(c);
}

/*
 * fpScanCommand
 * Scan data from the database(Redis & RocksDB)
 * --- Parameters ---
 *  arg1: Key(Table ID & PartitionInfo ID)
 *  arg2: Column IDs to find
 *
 * --- Usage Examples ---
 *  Parameters:
 *      key: "D:{3:1:2}"
 *          tableId: "3"
 *          partitionInfoId: "1:2"
 *      columnIds: ["2", "3", "4"]
 *  Command:
 *      redis-cli> FPSCAN D:{3:2:1} 2,3,4
 *  Results:
 *      redis-cli> "20180509"
 *      redis-cli> "Do young Kim"
 *      redis-cli> "Yonsei Univ"
 *      ...
 */
void fpScanCommand(client *c) {
    serverLog(LL_VERBOSE, "FPSCAN COMMAND START");
    serverLog(LL_DEBUG, "DEBUG: command parameter");
    serverLog(LL_DEBUG, "first: %s, second: %s", (sds) c->argv[1]->ptr,
              (sds) c->argv[2]->ptr);

    /*Creates scan parameters*/
    ScanParameter *scanParam = createScanParameter(c);
    serverLog(LL_DEBUG, "DEBUG: parse scan parameter");
    serverLog(LL_DEBUG, "startRowGroupId: %d, totalRowGroupCount: %d",
              scanParam->startRowGroupId, scanParam->totalRowGroupCount);
    serverLog(LL_DEBUG, "dataKeyInfo");
    serverLog(LL_DEBUG,
              "tableId: %d, partitionInfo: %s, rowGroupId: %d, rowCnt: %d",
              scanParam->dataKeyInfo->tableId,
              scanParam->dataKeyInfo->partitionInfo.partitionString,
              scanParam->dataKeyInfo->rowGroupId,
              scanParam->dataKeyInfo->row_number);
    serverLog(LL_DEBUG, "columnParam");
    serverLog(LL_DEBUG, "original: %s, columnCount: %d",
              scanParam->columnParam->original,
              scanParam->columnParam->columnCount);
    for (int i = 0; i < scanParam->columnParam->columnCount; ++i) {
        serverLog(LL_DEBUG, "i: %d, columnId: %ld, columnIdStr: %s",
                  i,
                  (long) vectorGet(&scanParam->columnParam->columnIdList, i),
                  (sds) vectorGet(
                      &scanParam->columnParam->columnIdStrList, i));
    }

    /*Populates row group information to scan parameters*/
    int totalDataCount = populateScanParameter(c->db, scanParam);
    serverLog(LL_DEBUG, "total data count: %d", totalDataCount);

    /*Load data from Redis or RocksDB*/
    Vector data;
    vectorTypeInit(&data, STL_TYPE_SDS);
    scanDataFromADDB(c->db, scanParam, &data);

    /*Scan data to client*/
    void *replylen = addDeferredMultiBulkLength(c);
    size_t numreplies = 0;
    serverLog(LL_DEBUG, "Loaded data from ADDB...");
    for (size_t i = 0; i < vectorCount(&data); ++i) {
        sds datum = sdsdup((sds) vectorGet(&data, i));
        serverLog(LL_DEBUG, "i: %zu, value: %s", i, datum);
        addReplyBulkSds(c, datum);
        numreplies++;
    }

    freeScanParameter(scanParam);
    vectorFree(&data);
    setDeferredMultiBulkLength(c, replylen, numreplies);
}

void _addReplyMetakeysResults(client *c, Vector *metakeys) {
    void *replylen = addDeferredMultiBulkLength(c);
    size_t numreplies = 0;
    serverLog(LL_DEBUG, "Loaded data from ADDB...");
    for (size_t i = 0; i < vectorCount(metakeys); ++i) {
        sds metakey = sdsdup((sds) vectorGet(metakeys, i));
        serverLog(LL_DEBUG, "i: %zu, metakey: %s", i, metakey);
        addReplyBulkSds(c, metakey);
        numreplies++;
    }

    setDeferredMultiBulkLength(c, replylen, numreplies);
    return;
}

/*
 * metakeysCommand
 *  Lookup key in metadict
 *  Filter partition from Metadict by parsed stack structure.
 * --- Parameters ---
 *  arg1: Parsed stack structure
 *
 * --- Usage Examples ---
 * --- Example 1: Pattern search only ---
 *  Parameters:
 *      pattern: *
 *  Command:
 *      redis-cli> METAKEYS *
 *  Results:
 *      redis-cli> "M:{30:2:0}"
 *      redis-cli> "M:{1:1:3:3:1}"
 *      redis-cli> "M:{100:2:0}"
 *      redis-cli> ...
 *  --- Example 2: Statements searching ---
 *  Parameters:
 *      pattern: M:{100:*}
 *      Statements:
 *          "3*2*EqualTo:2*2*EqualTo:Or:1*2*EqualTo:0*2*EqualTo:Or:Or:$"
 *          (select * from kv where 2=0 or 2=1 or 2=2 or 2=3;)
 *          "3*2*EqualTo:$1*2*EqualTo:0*2*EqualTo:Or:$"
 *          (Double Filtering)
 *          (select * from kv where col2=3)
 *          (select * from kv where col2=1 or col2=0)
 *  Command:
 *      redis-cli> METAKEYS M:{100:*} 3*2*EqualTo:2*2*EqualTo:Or:1*2*EqualTo:0*2*EqualTo:Or:Or:$
 *  Results:
 *      redis-cli> "M:{100:2:0}" // col2 = 0
 *      redis-cli> "M:{100:2:1}" // col2 = 1
 *      redis-cli> "M:{100:2:2}" // col2 = 2
 *      redis-cli> "M:{100:2:3}" // col2 = 3
 *      ...
 */
void metakeysCommand(client *c){
    /*Parses stringfied stack structure to readable parameters*/
    sds pattern = (sds) c->argv[1]->ptr;
    bool allkeys = (pattern[0] == '*' && pattern[1] == '\0');

    /*Initializes Vector for Metadict keys*/
    Vector metakeys;
    vectorTypeInit(&metakeys, STL_TYPE_SDS);

    /*Pattern match searching for metakeys*/
    dictIterator *di = dictGetSafeIterator(c->db->Metadict);
    dictEntry *de = NULL;
    while ((de = dictNext(di)) != NULL) {
        sds metakey = (sds) dictGetKey(de);
        if (
                allkeys ||
                stringmatchlen(pattern, sdslen(pattern), metakey,
                               sdslen(metakey), 0)
        ) {
            vectorAdd(&metakeys, metakey);
        }
    }
    dictReleaseIterator(di);

    /* If rawStatements is null or empty, prints pattern matching
     * results only...
     */
    if (c->argc < 3) {
        /*Prints out target partitions*/
        /*Scan data to client*/
        _addReplyMetakeysResults(c, &metakeys);
        vectorFree(&metakeys);
        return;
    }

    sds rawStatementsStr = (sds) c->argv[2]->ptr;

    if (!validateStatements(rawStatementsStr)) {
        serverLog(LL_WARNING, "[FILTER] Stack structure is not valid form: [%s]",
                  rawStatementsStr);
        addReplyErrorFormat(c, "[FILTER] Stack structure is not valid form: [%s]",
                            rawStatementsStr);
        return;
    }

    char copyStr[sdslen(rawStatementsStr) + 1];
    char *savePtr = NULL;
    char *token = NULL;
    memcpy(copyStr, rawStatementsStr, sdslen(rawStatementsStr) + 1);

    token = strtok_r(copyStr, PARTITION_FILTER_STATEMENT_SUFFIX, &savePtr);
    while (token != NULL) {
        Condition *root;
        sds rawStatementStr = sdsnew(token);

        if (parseStatement(rawStatementStr, &root) == C_ERR) {
            serverLog(
                    LL_WARNING,
                    "[FILTER][FATAL] Stack condition parser failed, server would have a memory leak...: [%s]",
                    rawStatementsStr);
            addReplyErrorFormat(
                    c,
                    "[FILTER][FATAL] Stack condition parser failed, server would have a memory leak...: [%s]",
                    rawStatementsStr);
            return;
        }

        serverLog(LL_DEBUG, "   ");
        serverLog(LL_DEBUG, "[FILTER][PARSE] Condition Tree");
        logCondition(root);

        Vector filteredMetakeys;
        vectorInit(&filteredMetakeys);
        for (size_t i = 0; i < vectorCount(&metakeys); ++i) {
            sds metakey = (sds) vectorGet(&metakeys, i);
            if (evaluateCondition(root, metakey)) {
                vectorAdd(&filteredMetakeys, metakey);
            }
        }
        vectorFree(&metakeys);
        metakeys = filteredMetakeys;

        sdsfree(rawStatementStr);
        freeConditions(root);

        token = strtok_r(NULL, PARTITION_FILTER_STATEMENT_SUFFIX, &savePtr);
    }

    /*Prints out target partitions*/
    /*Scan data to client*/
    _addReplyMetakeysResults(c, &metakeys);
    vectorFree(&metakeys);
}

/*Lookup the value list of field and field in dict*/
void fieldsAndValueCommand(client *c){

    dictIterator *di;
    dictEntry *de;
    sds pattern = sdsnew(c->argv[1]->ptr);

    robj *hashdict = lookupSDSKeyFordict(c->db, pattern);

    if(hashdict == NULL){
    	 addReply(c, shared.nullbulk);
    }
    else {

     	char str_buf[1024];
     	unsigned long numkeys = 0;
     	void *replylen = addDeferredMultiBulkLength(c);

     	dict *hashdictObj = (dict *) hashdict->ptr;
     	di = dictGetSafeIterator(hashdictObj);
     	while((de = dictNext(di)) != NULL){

     		sds key = dictGetKey(de);
     		sds val = dictGetVal(de);
     		sprintf(str_buf, "field : %s, value : %s", key, val);
     		addReplyBulkCString(c, str_buf);
     		numkeys++;

     		serverLog(LL_VERBOSE ,"key : %s , val : %s" , key,  val);

     }
     	sdsfree(pattern);
     	dictReleaseIterator(di);
     	setDeferredMultiBulkLength(c, replylen, numkeys);
    }

}

void prepareWriteToRocksDB(redisDb *db, robj *keyobj, robj *targetVal) {
	serverLog(LL_DEBUG, "PREPARING WRITE FOR ROCKSDB");
	dictIterator *di;
	dictEntry *de;
	char keystr[SDS_DATA_KEY_MAX];
	char *err = NULL;

	serverAssert(targetVal->location == LOCATION_FLUSH);
	rocksdb_writebatch_t *writeBatch = rocksdb_writebatch_create();

	dict *hashdictObj = (dict *) targetVal->ptr;
	if (hashdictObj == NULL)
		assert(0);

	di = dictGetSafeIterator(hashdictObj);
	if (di == NULL)
		assert(0);
	while ((de = dictNext(di)) != NULL) {
		sds field_key = dictGetKey(de);
		sds val = dictGetVal(de);

		sprintf(keystr, "%s:%s%s", keyobj->ptr, REL_MODEL_FIELD_PREFIX,
				field_key);
		sds rocksKey = sdsnew(keystr);
		robj *value = createStringObject(val, sdslen(val));
		setPersistentKeyWithBatch(db->persistent_store, rocksKey, sdslen(rocksKey),
				value->ptr, sdslen(value->ptr), writeBatch);
//		setPersistentKey(db->persistent_store, rocksKey,
//				sdslen(rocksKey), value->ptr, sdslen(value->ptr));
		sdsfree(rocksKey);
		decrRefCount(value);
	}
	rocksdb_write(db->persistent_store->ps, db->persistent_store->ps_options->woptions, writeBatch, &err);

	if (err) {
		serverLog(LL_VERBOSE, "RocksDB err");
		serverPanic("[PERSISTENT_STORE] putting a key failed");
	}

	rocksdb_writebatch_destroy(writeBatch);
	dictReleaseIterator(di);

}

void rocksdbkeyCommand(client *c){
    sds pattern = sdsnew(c->argv[1]->ptr);
    robj *key = createStringObject(pattern, sdslen(pattern));
    robj *hashdict = lookupSDSKeyFordict(c->db, pattern);
    prepareWriteToRocksDB(c->db, key,hashdict);
    sdsfree(pattern);
    decrRefCount(key);
    addReply(c, shared.ok);
}

void getRocksDBkeyAndValueCommand(client *c){

	sds pattern = sdsnew(c->argv[1]->ptr);

	  char* err = NULL;
	  size_t val_len;
	  char* val;
	  val = rocksdb_get_cf(c->db->persistent_store->ps, c->db->persistent_store->ps_options->roptions,
			  c->db->persistent_store->ps_cf_handles[PERSISTENT_STORE_CF_RW], pattern, sdslen(pattern), &val_len, &err);

	  if(val == NULL){
		  rocksdb_free(val);
		  serverLog(LL_VERBOSE, "ROCKSDB KEY-VALUE NOT EXIST");
			sdsfree(pattern);
			addReply(c, shared.err);
	  }
	  else {
		  robj *value = NULL;
		  value = createStringObject(val, val_len);
		  rocksdb_free(val);
		  serverLog(LL_VERBOSE, "ROCKSDB KEY : %s , VALUE : %s", pattern, (char *)value->ptr);
			sdsfree(pattern);
			decrRefCount(value);
			addReply(c, shared.ok);

	  }

}


void getQueueStatusCommand(client *c){
 	char str_buf[1024];
	if(c->db->EvictQueue->front == c->db->EvictQueue->rear){
		serverLog(LL_DEBUG, "EMPTY QUEUE");
     	unsigned long numkeys = 0;
     	void *replylen = addDeferredMultiBulkLength(c);
     	sprintf(str_buf, "Queue is empty, front : %d, rear : %d",c->db->EvictQueue->front ,c->db->EvictQueue->rear);
     addReplyBulkCString(c, str_buf);
     numkeys++;
     setDeferredMultiBulkLength(c, replylen, numkeys);
	}
	else {

     	unsigned long numkeys = 0;
     	void *replylen = addDeferredMultiBulkLength(c);

		int idx = c->db->EvictQueue->rear;
		int end = c->db->EvictQueue->front;
		while(idx != end){
		    dictIterator *di;
		    dictEntry *de = c->db->EvictQueue->buf[idx];

		    if(de == NULL){
		    	serverLog(LL_VERBOSE, "QUEUESTATUS COMMAND ENTRY IS NULL(front : %d, rear : %d, idx : %d)"
		    			,c->db->EvictQueue->front, c->db->EvictQueue->rear, idx);
		    }
		    sds key = dictGetKey(de);
		    robj *value = dictGetVal(de);
		    dict *hashdict = (dict *)value->ptr;

		    di = dictGetSafeIterator(hashdict);
		    dictEntry *de2;
		    while((de2 = dictNext(di)) != NULL){
		    	sds field_key = dictGetKey(de2);
		    	sds val = dictGetVal(de2);
		    	serverLog(LL_DEBUG, "GET QUEUE DataKey : %s Field : %s, Value : %s ", key ,field_key, val);
		    	sprintf(str_buf, "[Rear : %d, Front : %d, idx : %d]DataKey : %s Field : %s, Value : %s ]",
		    			c->db->EvictQueue->rear, c->db->EvictQueue->front ,idx,key,field_key, val);
	     		addReplyBulkCString(c, str_buf);
	     		numkeys++;
		    }
		    idx++;
		 	dictReleaseIterator(di);
		}
     	setDeferredMultiBulkLength(c, replylen, numkeys);
	}
}

void dequeueCommand(client *c){
	dictEntry *de = dequeue(c->db->EvictQueue);
	if(de == NULL){
		addReply(c, shared.err);
	}
	else {
		addReply(c, shared.ok);
		}
}

void getRearQueueCommand(client *c){

 	char str_buf[1024];


	dictEntry *de = c->db->EvictQueue->buf[c->db->EvictQueue->rear];
	serverLog(LL_DEBUG, "FRONT : %d, REAR : %d", c->db->EvictQueue->front, c->db->EvictQueue->rear);

	if(de == NULL){
	 	unsigned long numkeys = 0;
	 	void *replylen = addDeferredMultiBulkLength(c);
     	sprintf(str_buf, "Rear Entry is NULL");
     addReplyBulkCString(c, str_buf);
     numkeys++;
     setDeferredMultiBulkLength(c, replylen, numkeys);
	}
	else {
	 	unsigned long numkeys = 0;
	 	void *replylen = addDeferredMultiBulkLength(c);

		    dictIterator *di;
		    sds key = dictGetKey(de);
		    robj *value = dictGetVal(de);
		    dict *hashdict = (dict *)value->ptr;
		    di = dictGetSafeIterator(hashdict);
		    dictEntry *de2;
		    while((de2 = dictNext(di)) != NULL){
		    	sds field_key = dictGetKey(de2);
		    	sds val = dictGetVal(de2);
		    	serverLog(LL_DEBUG, "GET QUEUE Rear DataKey : %s Field : %s, Value : %s ", key ,field_key, val);
		    	sprintf(str_buf, "[Rear : %d]DataKey : %s Field : %s, Value : %s ]",
		    			c->db->EvictQueue->rear,key,field_key, val);
		 		addReplyBulkCString(c, str_buf);
		 		numkeys++;
		    }
		 	dictReleaseIterator(di);
		 	setDeferredMultiBulkLength(c, replylen, numkeys);

	}
}



void chooseBestKeyCommand(client *c){

	char str_buf[1024];

	if(c->db->EvictQueue->front == c->db->EvictQueue->rear){
     	unsigned long numkeys = 0;
     	void *replylen = addDeferredMultiBulkLength(c);
     	sprintf(str_buf, "Queue is empty, front : %d, rear : %d",c->db->EvictQueue->front ,c->db->EvictQueue->rear);
     addReplyBulkCString(c, str_buf);
     numkeys++;
     setDeferredMultiBulkLength(c, replylen, numkeys);

	}
	else {
     	unsigned long numkeys = 0;
     	void *replylen = addDeferredMultiBulkLength(c);

     	dictEntry *de = chooseBestKeyFromQueue(c->db->EvictQueue);
     	if(de != NULL){
     		dictIterator *di;
     		sds key = dictGetKey(de);
     		robj *value = dictGetVal(de);
     		dict *hashdict = (dict *)value->ptr;
     		di = dictGetSafeIterator(hashdict);
     		dictEntry *de2;

		    while((de2 = dictNext(di)) != NULL){
		    	sds field_key = dictGetKey(de2);
		    	sds val = dictGetVal(de2);
		    	serverLog(LL_DEBUG, "BestKey From Queue DataKey : %s Field : %s, Value : %s ", key ,field_key, val);
		    	sprintf(str_buf, "[Rear : %d]DataKey : %s Field : %s, Value : %s ]",
		    			c->db->EvictQueue->rear,key,field_key, val);
		 		addReplyBulkCString(c, str_buf);
		 		numkeys++;
		    }
		 	dictReleaseIterator(di);
		 	setDeferredMultiBulkLength(c, replylen, numkeys);
     	}
	}
}

void queueEmptyCommand(client *c){
	int j =0;

    for (j = 0; j < server.dbnum; j++){

    	redisDb *db = server.db + j;
    	initializeQueue(db->EvictQueue);
    	initializeQueue(db->FreeQueue);
   }
	addReply(c, shared.ok);
}

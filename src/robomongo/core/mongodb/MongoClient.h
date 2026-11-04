#pragma once

#include <mongo/client/dbclient_base.h>
#include <mongo/bson/bsonobj.h>

#include <functional>

#include "robomongo/core/Core.h"
#include "robomongo/core/domain/MongoQueryInfo.h"
#include "robomongo/core/domain/MongoAggregateInfo.h"
#include "robomongo/core/domain/MongoUser.h"
#include "robomongo/core/domain/MongoFunction.h"
#include "robomongo/core/events/MongoEventsInfo.h"

namespace Robomongo
{
    class MongoClient
    {
    public:
        MongoClient(mongo::DBClientBase *const scopedConnection);

        std::vector<std::string> getCollectionNamesWithDbname(const std::string &dbname) const;
        std::vector<std::string> getDatabaseNames() const;
        float getVersion() const;
        std::string dbVersionStr() const;
        std::string getStorageEngineType() const;

        std::vector<MongoUser> getUsers(const std::string &dbName);
        void createUser(const std::string &dbName, const MongoUser &user);
        void dropUser(const std::string &dbName, const std::string &user);

        std::vector<MongoFunction> getFunctions(const std::string &dbName) const;
        std::vector<IndexInfo> getIndexes(const MongoCollectionInfo &collection) const;
        void dropIndexFromCollection(const MongoCollectionInfo &collection, const std::string &indexName) const;
        void addEditIndex(const IndexInfo &oldInfo, const IndexInfo &newInfo) const;

        void renameIndexFromCollection(const MongoCollectionInfo &collection, const std::string &oldIndexName,
                                       const std::string &newIndexName) const;

        void createFunction(const std::string &dbName, const MongoFunction &fun,
                            const std::string &existingFunctionName = std::string());

        void dropFunction(const std::string &dbName, const std::string &name);
        void createDatabase(const std::string &dbName);
        void dropDatabase(const std::string &dbName);

        void createCollection(const std::string &ns, long long size, bool capped, int max, const mongo::BSONObj& extraOptions, mongo::BSONObj* info = nullptr);
        void renameCollection(const MongoNamespace &ns, const std::string &newCollectionName);
        void duplicateCollection(const MongoNamespace &ns, const std::string &newCollectionName);
        void dropCollection(const MongoNamespace &ns);
        void copyCollectionToDiffServer(mongo::DBClientBase *const, const MongoNamespace &from, const MongoNamespace &to);

        void insertDocument(const mongo::BSONObj &obj, const MongoNamespace &ns);
        void saveDocument(const mongo::BSONObj &obj, const MongoNamespace &ns);
        void removeDocuments(const MongoNamespace &ns, mongo::Query query, bool justOne = true);
        std::vector<MongoDocumentPtr> query(const MongoQueryInfo &info);

        /**
         * @brief Stream a full (unpaginated) query result to a JSON array file.
         *        onProgress is invoked periodically with the number of documents
         *        written so far. Returns the total number of documents written.
         */
        long long exportQueryJson(const MongoQueryInfo &info, const std::string &path,
                                  const std::function<void(long long)> &onProgress);

        /**
         * @brief Stream a full (unpaginated) aggregate pipeline result to a JSON
         *        array file. Refuses pipelines containing $out/$merge.
         */
        long long exportAggregateJson(const AggrInfo &aggrInfo, const std::string &path,
                                      const std::function<void(long long)> &onProgress);

        /**
         * @brief Import documents from a JSON file (array of extended-JSON
         *        objects, single object, or one object per line) into a
         *        collection, inserted in batches. onProgress(written, total).
         * @return number of documents imported
         */
        long long importJson(const std::string &dbName, const std::string &collectionName,
                             const std::string &path,
                             const std::function<void(long long, long long)> &onProgress);

        /**
         * @brief Server-side document count for a query (used to compute
         *        export progress percent when GUI doesn't know the total).
         */
        long long countDocuments(const MongoNamespace &ns, const mongo::BSONObj &query);

        /**
         * @brief Count for a find() query, bounded by maxTimeMS. Unwraps the
         *        legacy $query/special form. Throws on server error/timeout.
         */
        long long countQuery(const MongoQueryInfo &info, int maxTimeMS);

        /**
         * @brief Total count for an aggregate pipeline (appends $count),
         *        bounded by maxTimeMS. Throws on error/timeout.
         */
        long long countAggregate(const AggrInfo &aggrInfo, int maxTimeMS);

        MongoCollectionInfo runCollStatsCommand(const std::string &ns);
        std::vector<MongoCollectionInfo> runCollStatsCommand(const std::vector<std::string> &namespaces);

        void done();

    private:
        mongo::DBClientBase *const _dbclient;
        void checkLastErrorAndThrow(const std::string &db);
    };
}

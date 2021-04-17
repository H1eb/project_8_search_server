#pragma once
#include "search_server.h"

#include <deque>

class RequestQueue {
public:
    explicit RequestQueue(const SearchServer& search_server);
    
    template <typename DocumentPredicate>
    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentPredicate document_predicate){
        const auto result = search_server_.FindTopDocuments(raw_query, document_predicate);
        AddRequest(result.size());
        return result;
    }
    std::vector<Document> AddFindRequest(const std::string& raw_query, DocumentStatus status){
        const auto result = search_server_.FindTopDocuments(raw_query, status);
        AddRequest(result.size());
        return result;
    }
    std::vector<Document> AddFindRequest(const std::string& raw_query){
        const auto result = search_server_.FindTopDocuments(raw_query);
        AddRequest(result.size());
        return result;
    }
    
    int GetNoResultRequests() const;
    
private:
    struct QueryResult{
        int timestamp;
        int results;
    };
    std::deque<QueryResult> requests_;
    const SearchServer& search_server_;
    int no_result_requests_;
    int time_;
    const static int sec_in_day_ = 1440;
    void AddRequest(int result);
};
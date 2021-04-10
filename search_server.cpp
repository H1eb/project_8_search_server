#include "search_server.h"

#include <cmath>
#include <execution>
#include <iterator>

using namespace std;

SearchServer::SearchServer(string_view stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text))
    {
    }

SearchServer::SearchServer(const string& stop_words_text)
    : SearchServer(string_view(stop_words_text))
    {
    }

void SearchServer::AddDocument(int document_id, string_view document, DocumentStatus status, const vector<int>& ratings) {
    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw invalid_argument("Invalid document_id"s);
    }
    const auto [it, inserted] = documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status, string(document)});
    const auto words = SplitIntoWordsNoStop(it->second.text);

    const double inv_word_count = 1.0 / words.size();
    for (const string_view word : words) {
        word_to_document_freqs_[word][document_id] += inv_word_count;
        document_to_word_freqs_[document_id][word] += inv_word_count;
    }
    document_ids_.insert(document_id);
}

std::vector<Document> SearchServer::FindTopDocuments(string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(std::execution::seq, raw_query, status);
}

std::vector<Document> SearchServer::FindTopDocuments(string_view raw_query) const {
    return FindTopDocuments(std::execution::seq, raw_query);
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

void SearchServer::RemoveDocument(int document_id) {
    return RemoveDocument(execution::seq, document_id);
}

void SearchServer::RemoveDocument(const execution::sequenced_policy&, int document_id){
    if (document_to_word_freqs_.count(document_id) == 0)
        return;
    
    document_ids_.erase(document_id);
    documents_.erase(document_id);
    
    for (auto& [word, freq] : document_to_word_freqs_.at(document_id)) 
        word_to_document_freqs_.at(word).erase(document_id);
    
    document_to_word_freqs_.erase(document_id);
}

void SearchServer::RemoveDocument(const execution::parallel_policy&, int document_id) {
    if (document_to_word_freqs_.count(document_id) == 0)
        return;

    document_ids_.erase(document_id);
    documents_.erase(document_id);

    const auto& word_freqs = document_to_word_freqs_.at(document_id);
    vector<string_view> words(word_freqs.size());
    transform(
        execution::par,
        word_freqs.begin(), word_freqs.end(),
        words.begin(),
        [](const auto& item) { return item.first; }
    );
    for_each(
        execution::par,
        words.begin(), words.end(),
        [this, document_id](const string_view word) {
            word_to_document_freqs_.at(word).erase(document_id);
        });
    document_to_word_freqs_.erase(document_id);
}

set<int>::const_iterator SearchServer::begin() const {
    return document_ids_.begin();
}

set<int>::const_iterator SearchServer::end() const {
    return document_ids_.end();
}

const map<string_view, double>& SearchServer::GetWordFrequencies(int document_id) const{
    static const map <string_view, double> empty_map;
    if(document_to_word_freqs_.count(document_id) == 0)
        return empty_map;
    
    return document_to_word_freqs_.at(document_id);
}

tuple<std::vector<string_view>, DocumentStatus> SearchServer::MatchDocument(string_view raw_query, int document_id) const {
    return MatchDocument(execution::seq, raw_query, document_id);
}

tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const execution::sequenced_policy&, string_view raw_query, int document_id) const {
    const auto query = ParseQuery(raw_query);
		
		for (const string_view word : query.minus_words) {
			if (word_to_document_freqs_.count(word) == 0) {
				continue;
			}
			if (word_to_document_freqs_.at(word).count(document_id)) {
				return{{}, documents_.at(document_id).status};
			}
		}
		
		vector<string_view> matched_words;
		for (const string_view word : query.plus_words) {
			if (word_to_document_freqs_.count(word) == 0) {
				continue;
			}
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
    return {matched_words, documents_.at(document_id).status};
}

tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(const execution::parallel_policy&, string_view raw_query, int document_id) const{
    const auto query = ParseQuery(raw_query, true);
    
    const auto word_checker = 
        [this, document_id](const string_view word) {
            const auto it = word_to_document_freqs_.find(word);
            return it != word_to_document_freqs_.end() && it->second.count(document_id);
        };
    
    if (any_of(execution::par, query.minus_words.begin(), query.minus_words.end(), word_checker)) {
        return {{}, documents_.at(document_id).status};
    }
    
    vector<string_view> matched_words;
    auto words_end = copy_if(
        execution::par,
        query.plus_words.begin(), query.plus_words.end(),
        matched_words.begin(),
        word_checker
    );
    
    sort(matched_words.begin(), words_end);
    words_end = unique(matched_words.begin(), words_end);
    matched_words.erase(words_end, matched_words.end());

    return {matched_words, documents_.at(document_id).status};
    
}

bool SearchServer::IsStopWord(string_view word) const {
    return stop_words_.count(word) > 0;
}

bool SearchServer::IsValidWord(const string_view word) {
    return none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
    });
}

vector<string_view> SearchServer::SplitIntoWordsNoStop(string_view text) const {
    vector<string_view> words;
    for (const string_view word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw invalid_argument("Word "s + string(word) + " is invalid"s);
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(const vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    int rating_sum = 0;
    for (const int rating : ratings) {
        rating_sum += rating;
    }
    return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(string_view text) const {
    if (text.empty()) {
        throw invalid_argument("Query word is empty"s);
    }
    bool is_minus = false;
    if (text[0] == '-') {
        is_minus = true;
        text = text.substr(1);
    }
    if (text.empty() || text[0] == '-' || !IsValidWord(text)) {
        throw invalid_argument("_Query word "s + string(text) + " is invalid");
    }

    return {text, is_minus, IsStopWord(text)};
}

SearchServer::Query SearchServer::ParseQuery(string_view text, bool skip_sort) const {
    Query result;
    for (const string_view word : SplitIntoWords(text)) {
        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            } else {
                result.plus_words.push_back(query_word.data);
            }
        }
    }
	
	if (!skip_sort) {
        for (auto* words : {&result.plus_words, &result.minus_words}) {
            sort(words->begin(), words->end());
            words->erase(unique(words->begin(), words->end()), words->end());
        }
    }
    return result;
}

double SearchServer::ComputeWordInverseDocumentFreq(string_view word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}
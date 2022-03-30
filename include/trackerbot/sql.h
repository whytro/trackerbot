#ifndef TRACKERBOT_SQL_H
#define TRACKERBOT_SQL_H

#include "types.h"

#include <pqxx/pqxx>

#include <mutex> 
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class sql_handler {
public:
	sql_handler(std::string subreddit, std::string admin_credentials, std::string conn_string);

	struct Comment_Response {
		std::string comment_id;
		std::string thread_id;
		std::string author;
		int64_t epoch_time = 0;
		std::string comment_text;
	};
	bool admin_check_setup_status();

	std::unordered_map<std::string, Target> get_dev_map();

	void insert_thread(const std::string& thread_id, const std::string& sticky_id);
	void delete_thread(const std::string& thread_id);
	std::string get_thread_id(const std::string& comment_id);

	void insert_comment(const std::string& comment_id, const std::string& thread_id, 
		const std::string& dev, int status, const std::string& supervisor, int64_t supervisor_id, int64_t epoch_time, const std::string& comment_text);
	void update_comment(const std::string& comment_id, const std::string& text, int64_t modified_epoch);
	std::string change_comment_status(const std::string& comment_id, int status, const std::string& supervisor, int64_t supervisor_id);
	bool get_comment_status(const std::string& comment_id);
	void delete_comment(const std::string& comment_id);

	void insert_context(const std::string& context_id, const std::string& thread_id, const std::string& owner_id, bool status, const std::string& text);
	std::string get_context(const std::string& comment_id);
	std::unordered_map<std::string, std::string> get_contexts_for_thread(const std::string& thread_id);
	
	void enqueue_update(const std::string& thread_id);
	void dequeue_update(const std::string& thread_id);
	int update_queue_size(const std::string& thread_id);
	std::unordered_set<std::string> get_update_queue();
	
	void upsert_dev(const std::string& dev, const std::string& expertise, Target::Status status, const std::string& supervisor, int64_t supervisor_id);
	void update_dev_status(const std::string& dev, Target::Status new_status, const std::string& supervisor, int64_t supervisor_id);
	void update_dev_expertise(const std::string& dev, const std::string& expertise, const std::string& supervisor, int64_t supervisor_id);
	void delete_dev_expertise(const std::string& dev, const std::string& supervisor, int64_t supervisor_id);

	void insert_devedit_session(const std::string& dev, int64_t msg_id, int64_t channel_id);
	void delete_devedit_session(const std::string& dev);

	std::string get_sticky_id(const std::string& thread_id);

	bool check_comment_existence(const std::string& comment_id);		
	std::vector<Comment_Response> get_comments_in_thread(const std::string& thread_id);
	
	std::vector<std::string> get_thread_ids_by_date(int days);
	std::vector<std::string> get_comment_ids_by_date(int days);
	std::vector<std::string> get_comment_ids_by_thread_id(const std::string& thread_id);
	std::map<std::string, int64_t> get_comment_id_epoch_pair_by_date(int days);
	std::map<std::string, int64_t> get_comment_id_epoch_pair_by_thread_id(const std::string& thread_id);
	
	void begin_transaction();
	void commit_transaction();

private:
	std::string _target_subreddit;
	std::string _admin_login_string;
	std::string _connection_string;

	pqxx::connection* conn;
	std::mutex conn_mtx;

	pqxx::result admin_query(const std::string& query_string);
	bool admin_existence_query(const std::string& query_string);
	bool validate_subreddit_schema(const std::string& subreddit);

	enum class Prepareds {
		//Devs
		GET_DEV_MAP, GET_DEVEDITS_SESSIONS,
		//Threads & Comments (Inserts & Updates)
		INSERT_THREAD, DELETE_THREAD, GET_THREAD_ID,  
		INSERT_COMMENT, UPDATE_COMMENT, CHANGE_COMMENT_STATUS, GET_COMMENT_STATUS,
		DELETE_COMMENT,
		//Contexts
		INSERT_CONTEXT, GET_CONTEXT, GET_CONTEXTS_BY_THREAD,
		//Approval Queue
		ENQUEUE_APPROVAL, DEQUEUE_APPROVAL, CHECK_IF_QUEUED,
		//Update Queue
		ENQUEUE_UPDATE, DEQUEUE_UPDATE, UPDATE_QUEUE_SIZE, GET_UPDATE_QUEUE,
		//Devs
		UPSERT_DEV, UPDATE_DEV_STATUS, UPDATE_DEV_EXPERTISE, DELETE_DEV_EXPERTISE,
		INSERT_DEVEDIT_SESSION, DELETE_DEVEDIT_SESSION,
		//Sticky ID
		GET_STICKY_ID,
		//Comments
		CHECK_COMMENT_EXIST, GET_OTHER_COMMENTS, 
		GET_THREAD_IDS_BY_DATE, GET_COMMENT_IDS_BY_DATE, GET_COMMENT_IDS_BY_THREAD, 
		COMMENT_EPOCH_PAIRS_BY_DATE, COMMENT_EPOCH_PAIRS_BY_THREAD
	};
	struct Prep_Stm {
		std::string name;
		std::string statement;
	};
	std::map<Prepareds, Prep_Stm> prepared_statements;
};

#endif // TRACKERBOT_SQL_H
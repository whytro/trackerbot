#include "trackerbot/sql.h"

#include "trackerbot/types.h"

#include <pqxx/pqxx>
#include <spdlog/spdlog.h>

#include <mutex> 
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

sql_handler::sql_handler(std::string subreddit, std::string admin_credentials, std::string conn_string) 
	: _target_subreddit(std::move(subreddit))
	, _admin_login_string(std::move(admin_credentials))
	, _connection_string(std::move(conn_string))
{
	if(!admin_check_setup_status()) {
		spdlog::critical("Database is not Setup");
		return;
	}
	
	conn = new pqxx::connection{ _connection_string };

	if(!validate_subreddit_schema(_target_subreddit)) {
		spdlog::critical("Invalid Schema Setup for Subreddit! Restore");
		return;
	}

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};
	txn.exec0(fmt::format("SET search_path TO {};", _target_subreddit));
	conn_mtx.unlock();

	prepared_statements = {
		{ Prepareds::INSERT_THREAD,			    	{ "Insert_Thread",            "INSERT INTO threads(Thread_ID, Sticky_ID) VALUES($1, $2);" } },
		{ Prepareds::DELETE_THREAD,					{ "Delete_Thread",            "DELETE FROM threads WHERE Thread_ID = $1;" } },
		{ Prepareds::GET_THREAD_ID,					{ "Get_Thread_ID",            "SELECT Thread_ID FROM comments WHERE Comment_ID = $1 LIMIT 1;" } },

		{ Prepareds::INSERT_COMMENT,				{ "Insert_Comment",	          "INSERT INTO comments \
																				   (Comment_ID, Thread_ID, Dev_Username, Status, Supervisor_Username, Supervisor_ID, Post_Epoch, Comment_Text) \
																				   VALUES ($1, $2, $3, $4, $5, $6, $7, $8);" } },
		{ Prepareds::UPDATE_COMMENT,				{ "Update_Comment",			  "UPDATE comments SET Comment_Text = $1, Post_Epoch = $2 WHERE Comment_ID = $3 AND Post_Epoch < $2;" } },
		{ Prepareds::CHANGE_COMMENT_STATUS,			{ "Change_Comment_Status",	  "UPDATE comments SET Timestamp = CURRENT_TIMESTAMP, Status = $1, Supervisor_Username = $2, Supervisor_ID = $3 \
																				   WHERE Comment_ID = $4 RETURNING Thread_ID;" } },
		{ Prepareds::GET_COMMENT_STATUS,			{ "Get_Comment_Status",		  "SELECT status FROM comments WHERE comment_id = $1;" } },
		{ Prepareds::DELETE_COMMENT, 				{ "Delete_Comment",			  "DELETE FROM comments WHERE Comment_ID = $1;" } },

		{ Prepareds::INSERT_CONTEXT, 				{ "Insert_Context",			  "INSERT INTO contexts(Context_ID, Thread_ID, Owner_Comment_ID, Status, Comment_Text) VALUES ($1, $2, $3, $4, $5);" } },
		{ Prepareds::GET_CONTEXT, 					{ "Get_Context",			  "SELECT Comment_Text FROM contexts WHERE Owner_Comment_ID = $1 AND Status = true LIMIT 1;" } },
		{ Prepareds::GET_CONTEXTS_BY_THREAD, 		{ "Get_Context_By_Thread",	  "SELECT Owner_Comment_ID, Comment_Text FROM contexts WHERE thread_id = $1 AND Status = true;" } },

		{ Prepareds::ENQUEUE_UPDATE,				{ "Enqueue_Update",			  "INSERT INTO update_queue (Thread_ID) VALUES ($1) ON CONFLICT DO NOTHING;" } },
		{ Prepareds::DEQUEUE_UPDATE,				{ "Dequeue_Update",			  "DELETE FROM update_queue WHERE Thread_ID = $1;" } },
		{ Prepareds::UPDATE_QUEUE_SIZE,         	{ "Update_Queue_Size",		  "SELECT FROM update_queue WHERE Thread_ID = $1;" } },
		{ Prepareds::GET_UPDATE_QUEUE,          	{ "Get_Update_Queue",		  "SELECT Thread_ID FROM update_queue;" } },

		{ Prepareds::UPSERT_DEV,                	{ "Upsert_Dev",				  "INSERT INTO devs(Dev_Username, Expertise, Status, Supervisor_Username, Supervisor_ID, \
																				   Last_Modifier_Username, Last_Modifier_ID) \
																				   VALUES ($1, $2, $3, $4, $5, $4, $5) \
																				   ON CONFLICT (Dev_Username) DO UPDATE \
																				   SET Status = $3::SMALLINT, Last_Modifier_Username = $4, Last_Modifier_ID = $5, Last_Modified = CURRENT_TIMESTAMP;" } },
		{ Prepareds::UPDATE_DEV_STATUS,         	{ "Update_Dev_Status",		  "UPDATE devs SET Status = $1, Last_Modifier_Username = $2, Last_Modifier_ID = $3, Last_Modified = CURRENT_TIMESTAMP \
																		 		   WHERE Dev_Username = $4;" } },
		{ Prepareds::UPDATE_DEV_EXPERTISE,      	{ "Update_Dev_Expertise",	  "UPDATE devs SET Expertise = $1, Last_Modifier_Username = $2, Last_Modifier_ID = $3, Last_Modified = CURRENT_TIMESTAMP \
																				   WHERE Dev_Username = $4;" } },
		{ Prepareds::DELETE_DEV_EXPERTISE,			{ "Delete_Dev_Expertise",	  "UPDATE devs SET Expertise = '', Last_Modifier_Username = $1, Last_Modifier_ID = $2, Last_Modified = CURRENT_TIMESTAMP \
																				   WHERE Dev_Username = $3;" } },
		
		{ Prepareds::INSERT_DEVEDIT_SESSION,    	{ "Insert_DevEdit_Session",	  "INSERT INTO devedit_sessions(dev_username, managing_msg, msg_channel) \
																				   VALUES($1, $2, $3);" } },
		{ Prepareds::DELETE_DEVEDIT_SESSION,    	{ "Delete_DevEdit_Session",	  "DELETE FROM devedit_sessions WHERE dev_username = $1;" } },

		{ Prepareds::GET_STICKY_ID,                 { "Get_Sticky_ID",			  "SELECT Sticky_ID FROM threads WHERE Thread_ID = $1 LIMIT 1;" } },

		{ Prepareds::CHECK_COMMENT_EXIST,           { "Check_Comment_Exist",	  "SELECT EXISTS(SELECT 1 FROM comments WHERE comment_id = $1);" } },
		{ Prepareds::GET_OTHER_COMMENTS,            { "Get_Other_Comments",		  "SELECT Comment_ID, Thread_ID, Dev_Username, Post_Epoch, Comment_Text FROM comments WHERE Thread_ID = $1 AND Status = 1 \
																				   ORDER BY Post_Epoch DESC;" } },
		
		{ Prepareds::GET_THREAD_IDS_BY_DATE,        { "Thread_IDs_By_Date",       "SELECT Thread_ID FROM threads WHERE Timestamp > CURRENT_DATE - $1::SMALLINT ORDER BY Timestamp DESC LIMIT 100;" } },
		{ Prepareds::GET_COMMENT_IDS_BY_DATE,       { "Comments_By_Date",	      "SELECT Comment_ID FROM comments WHERE Timestamp > CURRENT_DATE - $1::SMALLINT \
																				   AND Status = 1 ORDER BY Post_Epoch DESC LIMIT 1000;" } },
		{ Prepareds::GET_COMMENT_IDS_BY_THREAD,     { "Comments_By_Thread",	      "SELECT Comment_ID FROM comments WHERE Thread_ID = $1 AND Status = 1 ORDER BY Post_Epoch DESC;" } },
		{ Prepareds::COMMENT_EPOCH_PAIRS_BY_DATE,   { "Comment_Epochs_By_Date",	  "SELECT Comment_ID, Post_Epoch FROM comments WHERE Timestamp > CURRENT_DATE - $1::SMALLINT AND Status = 1 \
																				   ORDER BY Post_Epoch DESC LIMIT 1000;" } },
		{ Prepareds::COMMENT_EPOCH_PAIRS_BY_THREAD, { "Comment_Epochs_By_Thread", "SELECT Comment_ID, Post_Epoch FROM comments WHERE thread_id = $1 AND STATUS = 1 ORDER BY Post_Epoch DESC;" } },
	};

	conn_mtx.lock();
	for(const auto& itr : prepared_statements) {
		const Prep_Stm& current_stm = itr.second;
		conn->prepare(current_stm.name, current_stm.statement);
	}
	conn_mtx.unlock();
}

pqxx::result sql_handler::admin_query(const std::string& query_string) {
	pqxx::connection admin_conn{ _admin_login_string };
	pqxx::nontransaction admin_txn{ admin_conn };

	return pqxx::result{ admin_txn.exec(query_string) };
}
bool sql_handler::admin_existence_query(const std::string& query_string) {
	pqxx::result r{ admin_query(query_string) };

	return r[0][0].as<bool>();
}
bool sql_handler::validate_subreddit_schema(const std::string& subreddit) {
	conn_mtx.lock();
	pqxx::work txn{*conn};
	pqxx::result r{ txn.exec(fmt::format("SELECT EXISTS(SELECT 1 FROM pg_namespace WHERE LOWER(nspname) = LOWER('{}'));", subreddit)) };
	conn_mtx.unlock();

	const bool exists = r[0][0].as<bool>();

	if(!exists) {
		throw std::runtime_error("SQL: Subreddit Table Doesn't Exist");
	}
	return exists;
}

bool sql_handler::admin_check_setup_status() {
	conn_mtx.lock();
	bool user_exists = admin_existence_query(fmt::format("SELECT EXISTS(SELECT 1 FROM pg_catalog.pg_roles WHERE rolname = '{}');", "rf_bot"));
	bool db_exists = admin_existence_query("SELECT EXISTS(SELECT 1 FROM pg_database where datname = 'rf_data');");
	conn_mtx.unlock();

	return user_exists && db_exists;
}

std::unordered_map<std::string, Target> sql_handler::get_dev_map() {
	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	pqxx::result devs{ txn.exec("SELECT Dev_Username, Expertise, Status FROM devs;") };
	pqxx::result edit_sessions{ txn.exec("SELECT Dev_Username, Managing_Msg, Msg_Channel FROM devedit_sessions;") };

	conn_mtx.unlock();

	std::unordered_map<std::string, Target> res;
	res.reserve(devs.size());

	for(const auto& row : devs) {
		Target current_dev;
		current_dev.data = std::make_shared<Target::Data>();
		current_dev.data->username = row[0].as<std::string>();
		current_dev.data->expertise = row[1].as<std::string>();
		current_dev.data->status = static_cast<Target::Status>(row[2].as<int>());

		res.emplace(current_dev.data->username, current_dev);
	}
	for(const auto& row : edit_sessions) {
		std::unordered_map<std::string, Target>::iterator itr = res.find(row[0].as<std::string>());
		if(itr == res.end()) {
			continue;
		}
		itr->second.data->managing_msg_id = row[1].as<int64_t>();
		itr->second.data->msg_channel = row[2].as<int64_t>();
	}
	
	return res;
}

void sql_handler::insert_thread(const std::string& thread_id, const std::string& sticky_id) {
	const std::string& stm = prepared_statements[Prepareds::INSERT_THREAD].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"INSERT INTO threads(Thread_ID, Sticky_ID) VALUES($1, $2);"
	txn.exec_prepared(stm, thread_id, sticky_id);
	conn_mtx.unlock();
}
void sql_handler::delete_thread(const std::string& thread_id) {
	const std::string& stm = prepared_statements[Prepareds::DELETE_THREAD].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"DELETE FROM threads WHERE Thread_ID = $1;"
	txn.exec_prepared(stm, thread_id);
	conn_mtx.unlock();
}
std::string sql_handler::get_thread_id(const std::string& comment_id) {
	const std::string& stm = prepared_statements[Prepareds::GET_THREAD_ID].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"SELECT Thread_ID FROM comments WHERE Comment_ID = $1 LIMIT 1;"
	pqxx::result r{ txn.exec_prepared(stm, comment_id) };
	conn_mtx.unlock();

	return r[0][0].as<std::string>();
}

void sql_handler::insert_comment(const std::string& comment_id, const std::string& thread_id, 
	const std::string& dev, int status, const std::string& supervisor, int64_t supervisor_id, int64_t epoch_time, const std::string& comment_text) 
{
	const std::string& stm = prepared_statements[Prepareds::INSERT_COMMENT].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"INSERT INTO comments \
	   (Comment_ID, Thread_ID, Dev_Username, Status, Supervisor_Username, Supervisor_ID, Post_Epoch, Comment_Text) \
	   VALUES ($1, $2, $3, $4, $5, $6, $7, $8);"
	txn.exec_prepared0(stm, comment_id, thread_id, dev, status, supervisor, supervisor_id, epoch_time, comment_text);
	conn_mtx.unlock();
}
void sql_handler::update_comment(const std::string& comment_id, const std::string& text, int64_t modified_epoch) {
	const std::string& stm = prepared_statements[Prepareds::UPDATE_COMMENT].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"UPDATE comments SET Comment_Text = $1, Post_Epoch = $2 WHERE Comment_ID = $3 AND Post_Epoch < $2;"
	txn.exec_prepared0(stm, text, modified_epoch, comment_id);
	conn_mtx.unlock();
}
std::string sql_handler::change_comment_status(const std::string& comment_id, int status, const std::string& supervisor, int64_t supervisor_id) {
	const std::string& stm = prepared_statements[Prepareds::CHANGE_COMMENT_STATUS].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"UPDATE comments SET Timestamp = CURRENT_TIMESTAMP, Status = $1, Supervisor_Username = $2, Supervisor_ID = $3 WHERE Comment_ID = $4 RETURNING Thread_ID;"
	pqxx::result r{ txn.exec_prepared(stm, status, supervisor, supervisor_id, comment_id) };
	conn_mtx.unlock();

	return r[0][0].as<std::string>();
}
bool sql_handler::get_comment_status(const std::string& comment_id) {
	const std::string& stm = prepared_statements[Prepareds::GET_COMMENT_STATUS].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};
	
	//"SELECT status FROM comments WHERE comment_id = $1;"
	pqxx::result r{ txn.exec_prepared(stm, comment_id) };
	conn_mtx.unlock();

	return r[0][0].as<bool>();
}
void sql_handler::delete_comment(const std::string& comment_id) {
	const std::string& stm = prepared_statements[Prepareds::DELETE_COMMENT].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"DELETE FROM comments WHERE Comment_ID = $1;"
	txn.exec_prepared0(stm, comment_id);
	conn_mtx.unlock();
}

void sql_handler::insert_context(const std::string& context_id, const std::string& thread_id, const std::string& owner_id, bool status, const std::string& text) {
	const std::string& stm = prepared_statements[Prepareds::INSERT_CONTEXT].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"INSERT INTO contexts(Context_ID, Thread_ID, Owner_Comment_ID, Status, Comment_Text) VALUES ($1, $2, $3, $4, $5);"
	txn.exec_prepared0(stm, context_id, thread_id, owner_id, status, text);
	conn_mtx.unlock();
}
std::string sql_handler::get_context(const std::string& comment_id) {
	const std::string& stm = prepared_statements[Prepareds::GET_CONTEXT].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"SELECT Comment_Text FROM contexts WHERE Owner_Comment_ID = $1 AND Status = true LIMIT 1;"
	pqxx::result r{ txn.exec_prepared(stm, comment_id) };
	conn_mtx.unlock();

	return r.empty() ? "" : r[0][0].as<std::string>();
}
std::unordered_map<std::string, std::string> sql_handler::get_contexts_for_thread(const std::string& thread_id) {
	const std::string& stm = prepared_statements[Prepareds::GET_CONTEXTS_BY_THREAD].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"SELECT Owner_Comment_ID, Comment_Text FROM contexts WHERE thread_id = $1 AND status = true;"
	pqxx::result r{ txn.exec_prepared(stm, thread_id) };
	conn_mtx.unlock();

	std::unordered_map<std::string, std::string> res;
	res.reserve(r.size());

    for(const auto& row : r) {
		std::string owner_comment_id = row[0].as<std::string>();
		std::string comment_text = row[1].as<std::string>();

		res.emplace(owner_comment_id, comment_text);
    }   
	
	return res;
}

void sql_handler::enqueue_update(const std::string& thread_id) {
	const std::string& stm = prepared_statements[Prepareds::ENQUEUE_UPDATE].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"INSERT INTO update_queue (Thread_ID) VALUES ($1) ON CONFLICT DO NOTHING;"
	txn.exec_prepared0(stm, thread_id);
	conn_mtx.unlock();
}
void sql_handler::dequeue_update(const std::string& thread_id) {
	const std::string& stm = prepared_statements[Prepareds::DEQUEUE_UPDATE].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//DELETE FROM update_queue WHERE Thread_ID = $1;
	txn.exec_prepared0(stm, thread_id);
	conn_mtx.unlock();
}
int sql_handler::update_queue_size(const std::string& thread_id) {
	const std::string& stm = prepared_statements[Prepareds::UPDATE_QUEUE_SIZE].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"SELECT FROM update_queue WHERE thread_id = $1;"
	pqxx::result r{ txn.exec_prepared(stm, thread_id) };
	conn_mtx.unlock();

	return r.size();
}
std::unordered_set<std::string> sql_handler::get_update_queue() {
	const std::string& stm = prepared_statements[Prepareds::GET_UPDATE_QUEUE].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"SELECT Thread_ID FROM update_queue;"
	pqxx::result r{ txn.exec_prepared(stm) };
	conn_mtx.unlock();

	std::unordered_set<std::string> res;
	res.reserve(r.size());

	for(const auto& row : r) {
		res.emplace(row[0].as<std::string>());
	}

	return res;
}

void sql_handler::upsert_dev(const std::string& dev, const std::string& expertise, Target::Status status, const std::string& supervisor, int64_t supervisor_id) {
	const std::string& stm = prepared_statements[Prepareds::UPSERT_DEV].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"INSERT INTO devs (Dev_Username, Expertise, Status, Supervisor_Username, Supervisor_ID, Last_Modifier_Username, Last_Modifier_ID) \
	   VALUES ($1, $2, $3, $4, $5, $4, $5) \
	   ON CONFLICT (Dev_Username) DO UPDATE \
	   SET Status = $3::SMALLINT, Last_Modifier_Username = $4, Last_Modifier_ID = $5, Last_Modified = CURRENT_TIMESTAMP;"
	txn.exec_prepared0(stm, dev, expertise, static_cast<int>(status), supervisor, supervisor_id);
	conn_mtx.unlock();
}
void sql_handler::update_dev_status(const std::string& dev, Target::Status new_status, const std::string& supervisor, int64_t supervisor_id) {
	const std::string& stm = prepared_statements[Prepareds::UPDATE_DEV_STATUS].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"UPDATE devs SET Status = $1, Last_Modifier_Username = $2, Last_Modifier_ID = $3, Last_Modified = CURRENT_TIMESTAMP \
	   WHERE Dev_Username = $4;"
	txn.exec_prepared0(stm, static_cast<int>(new_status), supervisor, supervisor_id, dev);
	conn_mtx.unlock();
}
void sql_handler::update_dev_expertise(const std::string& dev, const std::string& expertise, const std::string& supervisor, int64_t supervisor_id) {
	const std::string& stm = prepared_statements[Prepareds::UPDATE_DEV_EXPERTISE].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"UPDATE devs SET Expertise = $1, Last_Modifier_Username = $2, Last_Modifier_ID = $3, Last_Modified = CURRENT_TIMESTAMP \
       WHERE Dev_Username = $4;"
	txn.exec_prepared0(stm, expertise, supervisor, supervisor_id, dev);
	conn_mtx.unlock();
}
void sql_handler::delete_dev_expertise(const std::string& dev, const std::string& supervisor, int64_t supervisor_id) {
	const std::string& stm = prepared_statements[Prepareds::DELETE_DEV_EXPERTISE].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"UPDATE devs SET Expertise = '', Last_Modifier_Username = $1, Last_Modifier_ID = $2, Last_Modified = CURRENT_TIMESTAMP \
       WHERE Dev_Username = $3;"
	txn.exec_prepared0(stm, supervisor, supervisor_id, dev);
	conn_mtx.unlock();
}

void sql_handler::insert_devedit_session(const std::string& dev, int64_t msg_id, int64_t channel_id) {
	const std::string& stm = prepared_statements[Prepareds::INSERT_DEVEDIT_SESSION].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};
	
	//"INSERT INTO devedit_sessions(dev_username, managing_msg, msg_channel) \
	  VALUES($1, $2, $3);"
	txn.exec_prepared0(stm, dev, msg_id, channel_id);
	conn_mtx.unlock();
}
void sql_handler::delete_devedit_session(const std::string& dev) {
	const std::string& stm = prepared_statements[Prepareds::DELETE_DEVEDIT_SESSION].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"DELETE FROM devedit_sessions WHERE dev_username = $1;"
	txn.exec_prepared0(stm, dev);
	conn_mtx.unlock();
}

std::string sql_handler::get_sticky_id(const std::string& thread_id) {
	const std::string& stm = prepared_statements[Prepareds::GET_STICKY_ID].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"SELECT Sticky_ID FROM threads WHERE thread_id = $1::CHARACTER(7) LIMIT 1;
	pqxx::result r{ txn.exec_prepared(stm, thread_id) };
	conn_mtx.unlock();

	return r.empty() ? "" : r[0][0].as<std::string>();
}

bool sql_handler::check_comment_existence(const std::string& comment_id) {
	const std::string& stm = prepared_statements[Prepareds::CHECK_COMMENT_EXIST].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"SELECT EXISTS(SELECT 1 FROM comments AS a LEFT JOIN approval_queue AS b ON a.comment_id = b.comment_id WHERE a.comment_id = $1);"
	pqxx::result r{ txn.exec_prepared(stm, comment_id) };
	conn_mtx.unlock();

	return r[0][0].as<bool>();
}
std::vector<sql_handler::Comment_Response> sql_handler::get_comments_in_thread(const std::string& thread_id) {
	const std::string& stm = prepared_statements[Prepareds::GET_OTHER_COMMENTS].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"SELECT Comment_ID, Thread_ID, Dev_Username, Post_Epoch, Comment_Text FROM comments WHERE thread_id = $1::CHARACTER(7) AND status = true ORDER BY Post_Epoch DESC;"
	pqxx::result r{ txn.exec_prepared(stm, thread_id) };
	conn_mtx.unlock();

	std::vector<sql_handler::Comment_Response> res;
	res.reserve(r.size());

    for(const auto& row : r) {
		sql_handler::Comment_Response current_row;
		current_row.comment_id = row[0].as<std::string>();
		current_row.thread_id = row[1].as<std::string>();
		current_row.author = row[2].as<std::string>();
		current_row.epoch_time = row[3].as<int64_t>();
		current_row.comment_text = row[4].as<std::string>();

		res.emplace_back(current_row);
    }   
	
	return res;
}

std::vector<std::string> sql_handler::get_thread_ids_by_date(int days) {
	const std::string& stm = prepared_statements[Prepareds::GET_THREAD_IDS_BY_DATE].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"SELECT DISTINCT Thread_ID FROM comments WHERE Timestamp > CURRENT_DATE - $1::SMALLINT AND STATUS = TRUE ORDER BY Post_Epoch DESC LIMIT 1000;
	pqxx::result r{ txn.exec_prepared(stm, days) };
	conn_mtx.unlock();

	std::vector<std::string> res;
	res.reserve(r.size());

	for (const auto& row : r) {
		res.emplace_back(row[0].as<std::string>());
	}

	return res;
}
std::vector<std::string> sql_handler::get_comment_ids_by_date(int days) {
	const std::string& stm = prepared_statements[Prepareds::GET_COMMENT_IDS_BY_DATE].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"SELECT Comment_ID FROM comments WHERE Timestamp > CURRENT_DATE - $1::SMALLINT AND STATUS = TRUE ORDER BY Post_Epoch DESC LIMIT 1000;
	pqxx::result r{ txn.exec_prepared(stm, days) };
	conn_mtx.unlock();

	std::vector<std::string> res;
	res.reserve(r.size());
	
	for (const auto& row : r) {
		res.emplace_back(row[0].as<std::string>());
	}

	return res;
}
std::vector<std::string> sql_handler::get_comment_ids_by_thread_id(const std::string& thread_id) {
	const std::string& stm = prepared_statements[Prepareds::GET_COMMENT_IDS_BY_THREAD].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};
	
	//"SELECT Comment_ID FROM comments WHERE Thread_ID = $1::CHAR(6) AND Status = TRUE ORDER BY Post_Epoch DESC;"
	pqxx::result r{ txn.exec_prepared(stm, thread_id) };
	conn_mtx.unlock();

	std::vector<std::string> res;
	res.reserve(r.size());

	for(const auto& row : r) {
		res.emplace_back(row[0].as<std::string>());
    }   

	return res;
}
std::map<std::string, int64_t> sql_handler::get_comment_id_epoch_pair_by_date(int days) {
	const std::string& stm = prepared_statements[Prepareds::COMMENT_EPOCH_PAIRS_BY_DATE].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"SELECT Comment_ID, Post_Epoch FROM comments WHERE Timestamp > CURRENT_DATE - $1::SMALLINT AND STATUS = TRUE ORDER BY Post_Epoch DESC LIMIT 1000;"
	pqxx::result r{ txn.exec_prepared(stm, days) };
	conn_mtx.unlock();

	std::map<std::string, int64_t> res;

	for (const auto& row : r) {
		const std::string& comment_id = row[0].as<std::string>();
		const int64_t post_epoch = row[1].as<int64_t>();

		res.emplace(comment_id, post_epoch);
	}

	return res;
}
std::map<std::string, int64_t> sql_handler::get_comment_id_epoch_pair_by_thread_id(const std::string& thread_id) {
	const std::string& stm = prepared_statements[Prepareds::COMMENT_EPOCH_PAIRS_BY_THREAD].name;

	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};

	//"SELECT Comment_ID, Post_Epoch FROM comments WHERE thread_id = $1 AND STATUS = TRUE ORDER BY Post_Epoch DESC;"
	pqxx::result r{ txn.exec_prepared(stm, thread_id) };
	conn_mtx.unlock();

	std::map<std::string, int64_t> res;

	for (const auto& row : r) {
		const std::string& comment_id = row[0].as<std::string>();
		const int64_t post_epoch = row[1].as<int64_t>();

		res.emplace(comment_id, post_epoch);
	}

	return res;
}

void sql_handler::begin_transaction() {
	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};
	txn.exec0("BEGIN TRANSACTION;");
	conn_mtx.unlock();
}
void sql_handler::commit_transaction() {
	conn_mtx.lock();
	pqxx::nontransaction txn{*conn};
	txn.exec0("COMMIT TRANSACTION;");
	conn_mtx.unlock();
}

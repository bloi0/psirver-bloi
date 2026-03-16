#pragma once
#include <iostream>
#include <cstring>
#include <vector>
#include <unistd.h>

static constexpr char RN[] = "\r\n";
static constexpr char END_OF_HEADER[] = "\r\n\r\n";

void reply(int client, const char *status_line, const char *body);

class Task {
protected:
  // `int client` is the socket for sending the status back to the
  // client
  int client;
  
public:
  Task(int client) : client(client) {};
  virtual ~Task() {
    fsync(client);		// Just in case
    close(client);		// We _hope_ it closes
  };

  virtual int execute() = 0;	// Execute the task
  void execute_in_thread() {	// Execute and cleanup
    execute();
    delete this;
  }
  
  static Task *construct(int client,
			 const std::string& headers); // GET
  static Task *construct(int client,
			 const std::string& headers,
			 const std::string& body); // POST
}; 

class HealthTask : public Task { // GET /health
public:
  HealthTask(int client) : Task(client) {};
  int execute();
};

class TeapotTask : public Task { // GET /teapot
public:
  TeapotTask(int client) : Task(client) {};
  int execute();
};

class JobListTask : public Task { // GET /jobs
public:
  JobListTask(int client) : Task(client) {};
  int execute();
};

class ScriptListTask : public Task { // GET /scripts
public:
  ScriptListTask(int client) : Task(client) {};
  int execute();
};

class DeleteTask : public Task { // GET /scripts/<id>/delete
private:
  std::size_t script_id;
public:
  DeleteTask(int client, int id) : Task(client), script_id(id) {};
  int execute();
};

class JobStatusTask : public Task { // GET /jobs/<id>
private:
  std::size_t job_id;
public:
  JobStatusTask(int client, int id) : Task(client), job_id(id) {};
  int execute();
}; 

class TerminateTask : public Task { // GET /jobs/<id>/terminate
private:
  std::size_t job_id;
public:
  TerminateTask(int client, int id) : Task(client), job_id(id) {};
  int execute();
};

class StdoutTask : public Task { // GET /jobs/<id>/stdout
private:
  std::size_t job_id;
public:
  StdoutTask(int client, int id) : Task(client), job_id(id) {};
  int execute();
}; 

class StderrTask : public Task { // GET /jobs/<id>/stderr
private:
  std::size_t job_id;
public:
  StderrTask(int client, int id) : Task(client), job_id(id) {};
  int execute();
}; 

class RunTask : public Task { // POST /scripts/<id>/run + args
private:
  std::size_t script_id;
  std::vector<std::string> args;
public:
  RunTask(int client, int id, std::vector<std::string>args)
    : Task(client), script_id(id), args(args) {};
  int execute();
};

class UploadTask : public Task { // POST /scripts/upload
private:
  std::string filename;
  std::string script;
public:
  UploadTask(int client, std::string filename, std::string script)
    : Task(client), filename(filename), script(script) {};
  int execute();
  void cleanup(std::size_t which, const std::string& msg);
};


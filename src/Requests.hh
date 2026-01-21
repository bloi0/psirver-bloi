#pragma once
#include <iostream>
#include <cstring>

// Do not forget to recycle Requests after execution!
class Request {
public:
  virtual int execute() = 0;
  static Request *make_get_request(std::string headers);
  static Request *make_post_request(std::string headers, std::string body);
}; 

class HealthRequest : public Request { // GET /health
  int execute();
};

class ListRequest : public Request { // GET /jobs
  int execute();
};

class DeleteRequest : public Request { // GET /scripts/<id>/delete
  DeleteRequest(int id) { this->id = id; }
  int execute();
  int id;
};

class JobStatusRequest : public Request { // GET /jobs/<id>
  JobStatusRequest(int id) { this->id = id; }
  int execute();
  int id;
}; 

class TerminateRequest : public Request { // GET /jobs/<id>/terminate
  TerminateRequest(int id) { this->id = id; }
  int execute();
  int id;
};

class StdoutRequest : public Request { // GET /jobs/<id>/stdout
  StdoutRequest(int id) { this->id = id; }
  int execute();
  int id;
}; 

class StderrRequest : public Request { // GET /jobs/<id>/stderr
  StderrRequest(int id) { this->id = id; }
  int execute();
  int id;
}; 

class RunRequest : public Request { // POST /scripts/<id>/run + args
  RunRequest(std::string headers, std::string body);
  int execute();
  int id;
  std::string args;
};

class UploadRequest : public Request { // POST /scripts/upload
public:
  UploadRequest(std::string headers, std::string body);
  std::string script;
  int execute();
};


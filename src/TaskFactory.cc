#include <unordered_map>
#include <functional>
#include <sstream>
#include "Tasks.hh"

// Task factory (Factory Method pattern,
// https://refactoring.guru/design-patterns/factory-method/cpp/example).
// Parses an HTTP request (headers and, when applicable, body) and
// dispatches it to a concrete Task subclass based on the request path
// and parameters.

/// Extract the request path from the first line of an HTTP request in
/// `headers`. Expects a request line of the form: "<METHOD> <PATH>
/// <VERSION>\r\n...".  Returns the extracted path (which must be
/// non-empty and start with '/') or an empty string on parse failure.

static const std::string get_path_from(const std::string& headers)
{
  const std::size_t sp1 = headers.find(' ');
  if (sp1 == std::string::npos) {
    return {};
  }

  const std::size_t start = sp1 + 1;
  if (start >= headers.size() || headers[start] != '/') {
    return {};
  }
  
  const std::size_t sp2 = headers.find(' ', start);
  if (sp2 == std::string::npos || sp2 == start) {
    return {};
  }
  
  return headers.substr(start, sp2 - start);
  // Ideally, we should check the protocol version (HTTP/1.1) but we
  // assume the headers are well-formed.
}

/// Factory/dispatcher for GET requests: extracts the request path
/// from `headers` and constructs the corresponding Task for
/// `client`. Supports exact routes (/health, /jobs, /scripts,
/// /teapot) and parameterized routes /jobs/<id> (status) and
/// /jobs/<id>/(stdout|stderr|terminate), as well as
/// /scripts/<id>/delete. Returns nullptr if the request line/path is
/// malformed, the id is not a valid integer, or the route/action is
/// unsupported.
Task *Task::construct(int client, const std::string& headers)
{
  // Extract the path
  auto path = get_path_from(headers);
  if (path.empty()) {
    return nullptr;
  }  

  // Exact-match routes (zero-arg tasks)
  using Factory0 = std::function<Task*(int)>;
  
  static const std::unordered_map<std::string, Factory0> routes = {
    {"/health",  [](int client){ return new HealthTask(client); }},
    {"/jobs",    [](int client){ return new JobListTask(client); }},
    {"/scripts", [](int client){ return new ScriptListTask(client); }},
    {"/teapot",  [](int client){ return new TeapotTask(client); }},
  };

  auto it = routes.find(path);
  if (it != routes.end()) {	// found!
    return it->second(client);
  }

  // The task takes arguments. Then it must be /jobs or /scripts
  static constexpr char JOBS[] = "/jobs";
  const std::size_t JOBS_LEN = sizeof(JOBS) - 1;
  if(path.compare(0, JOBS_LEN, JOBS) == 0) {
    const std::string rest = path.substr(JOBS_LEN);
    if(rest[0] != '/') {
      return nullptr;
    }
    
    const std::size_t slash = rest.find("/", 1);
    int job_id;
    if(slash == std::string::npos) {
      try {
	job_id = std::stoi(rest.substr(1));
      } catch (...) {
	return nullptr; 
      }
      return new JobStatusTask(client, job_id);      
    }

    // One of the job action tasks
    try {
      job_id = std::stoi(rest.substr(1, slash - 1));
    } catch (...) { return nullptr; }
    
    using Factory1 = std::function<Task*(int,int)>;

    static const std::unordered_map<std::string, Factory1> actions = {
      {"stderr",    [](int client, int job){ return new StderrTask(client, job); }},
      {"stdout",    [](int client, int job){ return new StdoutTask(client, job); }},
      {"terminate", [](int client, int job){ return new TerminateTask(client, job); }},
    };

    auto action = rest.substr(slash + 1);
    auto it = actions.find(action);
    if (it != actions.end()) {
      return it->second(client, job_id);
    }
    
    // Invalid job action
    return nullptr;
  } 
  
  // Return a new Script Task w/action
  static constexpr char SCRIPTS[] = "/scripts";
  const std::size_t SCRIPTS_LEN = sizeof(SCRIPTS) - 1;
  if(path.compare(0, SCRIPTS_LEN, SCRIPTS) == 0) {
    std::string rest = path.substr(SCRIPTS_LEN);
    if(rest[0] != '/') {
      return nullptr;
    }
    
    std::size_t slash = rest.find("/", 1);
    if(slash == std::string::npos) {
      return nullptr;
    }
    
    int script_id;
    try {
      script_id = std::stoi(rest.substr(1, slash - 1));
    } catch (...) {
      return nullptr;
    }

    // Only one action is allowed
    const std::string action = rest.substr(slash + 1);
    if (action == "delete") {
      return new DeleteTask(client, script_id);
    }

    // Invalid script action
    return nullptr;
  }

  // Invalid action
  return nullptr;
}

/// Parse a multipart/form-data upload request, extract the uploaded
/// file name and contents from the given headers/body, and return a
/// newly created UploadTask; returns nullptr if the request is
/// malformed or missing data.

static Task *new_upload_task(int client,
			     const std::string& headers,
			     const std::string& body)
{
  // Content-Type: multipart/form-data; boundary=------------------------67c1112af97a18b9
  // Body: 
  // --------------------------67c1112af97a18b9
  // Content-Disposition: form-data; name="file"; filename="Makefile"
  // Content-Type: application/octet-stream
  // ......data.........
  // --------------------------67c1112af97a18b9--
  
  // Confirm the content type
  static constexpr char CT[] = "Content-Type: multipart/form-data; boundary=";
  const std::size_t CT_LEN = sizeof(CT) - 1;
 
  const std::size_t ct_pos = headers.find(CT);
  if(ct_pos == std::string::npos) {
    return nullptr;
  }
  
  auto boundary = headers.substr(ct_pos + CT_LEN);
  const std::size_t ct_end = boundary.find("\r\n");
  if(ct_end != std::string::npos) {
    boundary = boundary.substr(0, ct_end);
  }
  
  // Extract the file from the body
  const std::size_t part_start = body.find("--" + boundary);
  if(part_start == std::string::npos) {
    return nullptr;
  }
  
  auto data_start = body.find(END_OF_HEADER, part_start);
  if(data_start == std::string::npos) {
    return nullptr;
  }
  data_start += sizeof(END_OF_HEADER) - 1;		// skip END_OF_HEADER
  
  const std::size_t data_end = body.find("\r\n--" + boundary + "--");
  if(data_end == std::string::npos) {
    return nullptr;
  }

  // Extract the file name
  static constexpr char CD[] = "Content-Disposition: form-data; name=\"file\"; filename=\"";
  const std::size_t CD_LEN = sizeof(CD) - 1;
  auto fname_start = body.find(CD);
  if(fname_start == std::string::npos) {
    return nullptr;
  }  
  fname_start += CD_LEN;
  
  const std::size_t fname_end = body.find('"', fname_start);
  if(fname_end == std::string::npos) {
    return nullptr;
  }
  
  const std::string filename = body.substr(fname_start, fname_end - fname_start);
  const std::string script =   body.substr( data_start,  data_end -  data_start);
  
  return new UploadTask(client, filename, script);     
}

/// Parse a /scripts/<id>/run request: validate the action and
/// Content-Type, extract the numeric script id from `rest` and the
/// comma-separated `args=` list from the request body, and return a
/// newly created RunTask; returns nullptr on malformed/unsupported
/// input.
static Task *new_run_task(int client,
			  const std::string& headers,
			  const std::string& body,
			  const std::string& rest)
{
  const std::size_t slash = rest.find("/");
  if(slash == std::string::npos || slash == 0 || slash + 1 >= rest.size()) {
    return nullptr;
  }
  auto action = rest.substr(slash + 1);
  
  if (action != "run") {    
    return nullptr;
  }
  
  int script_id;
  try {
    script_id = std::stoi(rest.substr(0, slash));
  } catch (...) {
    return nullptr;
  }
  
  // Content-Type: application/x-www-form-urlencoded
  // Body:
  // args=arg1,arg2,arg3

  // Confirm the content type
  static constexpr char CT[] = "Content-Type: application/x-www-form-urlencoded";
  const std::size_t ct_pos = headers.find(CT);
  if(ct_pos == std::string::npos) {
    return nullptr;
  }
  
  // Extract the arglist from the body
  static constexpr char ARGS[] = "args=";  
  const size_t ARGS_LEN = sizeof(ARGS) - 1;  
  const std::size_t args_pos = body.find(ARGS);
  if(args_pos == std::string::npos) {
    return nullptr;
  }
  
  // Comma-separated list
  // We assume that arguments do not have commas
  std::vector<std::string>args;
  args.reserve(10);		// A random guess
  std::stringstream arglist(body.substr(args_pos + ARGS_LEN));
  std::string item;
  while (std::getline(arglist, item, ',')) {
    args.push_back(item);
  }
  
  return new RunTask(client, script_id, args);
}


/// Factory/dispatcher: extracts the request path from headers and
/// creates a /scripts/ task (e.g., upload or run) using the provided
/// client, headers, and body; returns nullptr on failure/unknown path

Task *Task::construct(int client,
		      const std::string& headers,
		      const std::string& body)
{
  // Extract the path
  const std::string path = get_path_from(headers);
  if (path.empty()) {
    return nullptr;
  }
  
  static constexpr char SCRIPTS[] = "/scripts/";
  const std::size_t SCRIPTS_LEN = sizeof(SCRIPTS) - 1;
  
  if(path.size() < SCRIPTS_LEN ||
     path.compare(0, SCRIPTS_LEN, SCRIPTS) != 0) {
    // Not /scripts
    return nullptr;
  }
  
  auto action = path.substr(SCRIPTS_LEN);
  
  if(action == "upload") {	// /scripts/upload
    return new_upload_task(client, headers, body);
  }
  
  // /scripts/<id>/run
  return new_run_task(client, headers, body, action);
}

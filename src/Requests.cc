#include "Requests.hh"

int StderrRequest::execute()
{
  // --> Implement
  return 0;
}

int DeleteRequest::execute()
{
  // --> Implement
  return 0;
}

int RunRequest::execute()
{
  // --> Implement
  return 0;
}

int JobStatusRequest::execute()
{
  // --> Implement
  return 0;
};

int TerminateRequest::execute()
{
  // --> Implement
  return 0;
}

int StdoutRequest::execute()
{
  // --> Implement
  return 0;
} 

int UploadRequest::execute()
{
  // --> Implement
  return 0;
}

int ListRequest::execute()
{
  // --> Implement
  return 0;
}

int HealthRequest::execute()
{
  // --> Implement
  return 0;
}
 
UploadRequest::UploadRequest(std::string headers, std::string body) {
  // --> Implement
}

RunRequest::RunRequest(std::string headers, std::string body) {
  // --> Implement
}

// This function parses the headers and returns one of the GET request
// objects
Request *Request::make_get_request(std::string headers)
{
  // --> Implement & remove debug printout
  std::cerr << "GET" << std::endl; // DEBUG
  std::cerr << "Headers:\n" << headers << std::endl; // DEBUG
  return nullptr;
}

// This function parses the headers and the body returns one of the
// POST request objects
Request *Request::make_post_request(std::string headers, std::string body)
{
  // Content-Type: application/x-www-form-urlencoded
  // Body:
  // ......data.........
  // 
  // or:
  //
  // Content-Type: multipart/form-data; boundary=------------------------67c1112af97a18b9
  // Body: 
  // --------------------------67c1112af97a18b9
  // Content-Disposition: form-data; name="file"; filename="Makefile"
  // Content-Type: application/octet-stream
  // ......data.........
  // --------------------------67c1112af97a18b9--

  // --> Implement & remove debug printout
  std::cerr << "POST" << std::endl; // DEBUG
  std::cerr << "Headers:\n" << headers << std::endl; // DEBUG
  std::cerr << "Body:\n" << body << std::endl; // DEBUG
  return nullptr;
}


#include "Tasks.hh"

int HealthTask::execute()
{
  // --> Implement later
  return 0;
}
 
int TeapotTask::execute()
{
  // --> Implement later
  return 0;
}

int StderrTask::execute()
{
  // --> Implement later
  return 0;
}

int DeleteTask::execute()
{
  // --> Implement later
  return 0;
}

int RunTask::execute()
{
  // --> Implement later
  return 0;
}

int JobStatusTask::execute()
{
  // --> Implement later
  return 0;
};

int TerminateTask::execute()
{
  // --> Implement later
  return 0;
}

int StdoutTask::execute()
{
  // --> Implement later
  return 0;
} 

int UploadTask::execute()
{
  // --> Implement later
  return 0;
}

int ScriptListTask::execute()
{
  // --> Implement later
  return 0;
}

int JobListTask::execute()
{
  // --> Implement later
  return 0;
}

// This function parses the headers and returns one of the GET task
// objects
Task *Task::construct(int /*client*/, std::string headers)
{
  // --> Implement & remove debug printout
  std::cerr << "GET" << std::endl; // DEBUG
  std::cerr << "Headers:\n" << headers << std::endl; // DEBUG
  
  // Return a new Task
  return nullptr;
}

// This function parses the headers and the body returns one of the
// POST task objects
Task *Task::construct(int /*client*/, std::string headers, std::string body)
{
  // Content-Type: application/x-www-form-urlencoded
  // Body:
  // ......data.........

  // or (note that the boundary is the same in all three places):

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
  
  // Return a new Task
  return nullptr;
}


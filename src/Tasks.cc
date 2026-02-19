#include "Tasks.hh"

// Do the job, reply to the client, and return to the main loop

int HealthTask::execute()
{
  // --> To be implemented later
  std::cerr << "I will report health\n";
  return 0;
}
 
int TeapotTask::execute()
{
  // --> To be implemented later
  std::cerr << "I am a teapot (maybe)\n";
  return 0;
}

int StderrTask::execute()
{
  // --> To be implemented later
  std::cerr << "I will report stderr of job " << job_id << "\n";
  return 0;
}

int DeleteTask::execute()
{
  // --> To be implemented later
  std::cerr << "I will delete script " << script_id << "\n";
  return 0;
}

int RunTask::execute()
{
  // --> To be implemented later
  std::cerr << "I will run script " << script_id << "\n";
  return 0;
}

int JobStatusTask::execute()
{
  // --> To be implemented later
  std::cerr << "I will report status of job " << job_id << "\n";
  return 0;
};

int TerminateTask::execute()
{
  // --> To be implemented later
  std::cerr << "I will terminate job " << job_id << "\n";
  return 0;
}

int StdoutTask::execute()
{
  // --> To be implemented later
  std::cerr << "I will report stdout of job " << job_id << "\n";
  return 0;
} 

int UploadTask::execute()
{
  // --> To be implemented later
  std::cerr << "I will upload file " << filename << "; script length " << script.size() << "\n";
  return 0;
}

int ScriptListTask::execute()
{
  // --> To be implemented later
  std::cerr << "I will report list of scripts\n";
  return 0;
}

int JobListTask::execute()
{
  // --> To be implemented later
  std::cerr << "I will report list of jobs\n";
  return 0;
}

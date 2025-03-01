/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkSMPToolsImpl.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "SMP/Common/vtkSMPToolsImpl.h"
#include "SMP/STDThread/vtkSMPToolsImpl.txx"

#include <cstdlib> // For std::getenv()
#include <stack>   // For std::stack
#include <thread>  // For std::thread::hardware_concurrency()

namespace vtk
{
namespace detail
{
namespace smp
{
VTK_ABI_NAMESPACE_BEGIN
static int specifiedNumThreads = 0;
static std::stack<std::thread::id> threadIdStack;
static std::mutex threadIdStackLock;

//------------------------------------------------------------------------------
template <>
void vtkSMPToolsImpl<BackendType::STDThread>::Initialize(int numThreads)
{
  const int maxThreads = std::thread::hardware_concurrency();
  if (numThreads == 0)
  {
    const char* vtkSmpNumThreads = std::getenv("VTK_SMP_MAX_THREADS");
    if (vtkSmpNumThreads)
    {
      numThreads = std::atoi(vtkSmpNumThreads);
    }
    else
    {
      specifiedNumThreads = 0;
    }
  }
  if (numThreads > 0)
  {
    numThreads = std::min(numThreads, maxThreads);
    specifiedNumThreads = numThreads;
  }
}

//------------------------------------------------------------------------------
int GetNumberOfThreadsSTDThread()
{
  return specifiedNumThreads ? specifiedNumThreads : std::thread::hardware_concurrency();
}

//------------------------------------------------------------------------------
void PushThreadId(std::thread::id id)
{
  threadIdStackLock.lock();
  threadIdStack.emplace(id);
  threadIdStackLock.unlock();
}

//------------------------------------------------------------------------------
void PopThreadId()
{
  threadIdStackLock.lock();
  threadIdStack.pop();
  threadIdStackLock.unlock();
}

//------------------------------------------------------------------------------
bool GetSingleThreadSTDThread()
{
  return threadIdStack.top() == std::this_thread::get_id();
}

//------------------------------------------------------------------------------
template <>
int vtkSMPToolsImpl<BackendType::STDThread>::GetEstimatedNumberOfThreads()
{
  return GetNumberOfThreadsSTDThread();
}

//------------------------------------------------------------------------------
template <>
bool vtkSMPToolsImpl<BackendType::STDThread>::GetSingleThread()
{
  return GetSingleThreadSTDThread();
}

VTK_ABI_NAMESPACE_END
} // namespace smp
} // namespace detail
} // namespace vtk

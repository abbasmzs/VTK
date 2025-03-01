/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkmClipInstantiationsWithImplicitFunction.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "vtkmClipInternals.h"
#include "vtkmlib/DataSetConverters.h"

#include <vtkm/filter/contour/ClipWithImplicitFunction.h>

//------------------------------------------------------------------------------
VTK_ABI_NAMESPACE_BEGIN
vtkm::cont::DataSet vtkmClip::internals::ExecuteClipWithImplicitFunction(vtkm::cont::DataSet& in)
{
  auto function = this->ClipFunctionConverter->Get();

  vtkm::cont::DataSet result;
  vtkm::filter::contour::ClipWithImplicitFunction functionFilter;
  functionFilter.SetImplicitFunction(function);
  result = functionFilter.Execute(in);

  return result;
}
VTK_ABI_NAMESPACE_END

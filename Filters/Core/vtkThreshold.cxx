/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkThreshold.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "vtkThreshold.h"

#include "vtkCell.h"
#include "vtkCellData.h"
#include "vtkCellIterator.h"
#include "vtkDataSetAttributes.h"
#include "vtkIdList.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMath.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkStreamingDemandDrivenPipeline.h"
#include "vtkUnsignedCharArray.h"
#include "vtkUnstructuredGrid.h"

#include <algorithm>
#include <limits>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkThreshold);

// Construct with lower threshold=0, upper threshold=1, and threshold
// function=upper AllScalars=1.
vtkThreshold::vtkThreshold()
{
  this->LowerThreshold = -std::numeric_limits<double>::infinity();
  this->UpperThreshold = std::numeric_limits<double>::infinity();

  // by default process active point scalars
  this->SetInputArrayToProcess(
    0, 0, 0, vtkDataObject::FIELD_ASSOCIATION_POINTS_THEN_CELLS, vtkDataSetAttributes::SCALARS);
}

vtkThreshold::~vtkThreshold() = default;

//------------------------------------------------------------------------------
int vtkThreshold::Lower(double s) const
{
  return (s <= this->LowerThreshold ? 1 : 0);
}

//------------------------------------------------------------------------------
int vtkThreshold::Upper(double s) const
{
  return (s >= this->UpperThreshold ? 1 : 0);
}

//------------------------------------------------------------------------------
int vtkThreshold::Between(double s) const
{
  return (s >= this->LowerThreshold ? (s <= this->UpperThreshold ? 1 : 0) : 0);
}

//------------------------------------------------------------------------------
void vtkThreshold::SetThresholdFunction(int function)
{
  if (this->GetThresholdFunction() != function)
  {
    switch (function)
    {
      case vtkThreshold::THRESHOLD_BETWEEN:
        this->ThresholdFunction = &vtkThreshold::Between;
        break;
      case vtkThreshold::THRESHOLD_LOWER:
        this->ThresholdFunction = &vtkThreshold::Lower;
        break;
      case vtkThreshold::THRESHOLD_UPPER:
        this->ThresholdFunction = &vtkThreshold::Upper;
        break;
    }

    this->Modified();
  }
}

//------------------------------------------------------------------------------
int vtkThreshold::GetThresholdFunction()
{
  if (this->ThresholdFunction == &vtkThreshold::Between)
  {
    return vtkThreshold::THRESHOLD_BETWEEN;
  }
  else if (this->ThresholdFunction == &vtkThreshold::Lower)
  {
    return vtkThreshold::THRESHOLD_LOWER;
  }
  else if (this->ThresholdFunction == &vtkThreshold::Upper)
  {
    return vtkThreshold::THRESHOLD_UPPER;
  }

  // Added to avoid warning. Should never be reached.
  return -1;
}

int vtkThreshold::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // get the info objects
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  vtkInformation* outInfo = outputVector->GetInformationObject(0);

  // get the input and output
  vtkDataSet* input = vtkDataSet::SafeDownCast(inInfo->Get(vtkDataObject::DATA_OBJECT()));
  vtkUnstructuredGrid* output =
    vtkUnstructuredGrid::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));

  vtkPointData *pd = input->GetPointData(), *outPD = output->GetPointData();
  vtkCellData *cd = input->GetCellData(), *outCD = output->GetCellData();

  vtkDebugMacro(<< "Executing threshold filter");

  if (this->AttributeMode != -1)
  {
    vtkErrorMacro(<< "You have set the attribute mode on vtkThreshold. This method is deprecated, "
                     "please use SetInputArrayToProcess instead.");
    return 1;
  }

  vtkDataArray* inScalars = this->GetInputArrayToProcess(0, inputVector);

  if (!inScalars)
  {
    vtkDebugMacro(<< "No scalar data to threshold");
    return 1;
  }

  outPD->CopyGlobalIdsOn();
  outPD->CopyAllocate(pd);
  outCD->CopyGlobalIdsOn();
  outCD->CopyAllocate(cd);

  vtkIdType numPts = input->GetNumberOfPoints();
  output->Allocate(input->GetNumberOfCells());

  vtkSmartPointer<vtkPoints> newPoints = vtkSmartPointer<vtkPoints>::Take(vtkPoints::New());

  // set precision for the points in the output
  if (this->OutputPointsPrecision == vtkAlgorithm::DEFAULT_PRECISION)
  {
    vtkPointSet* inputPointSet = vtkPointSet::SafeDownCast(input);
    if (inputPointSet && inputPointSet->GetPoints())
    {
      newPoints->SetDataType(inputPointSet->GetPoints()->GetDataType());
    }
    else
    {
      newPoints->SetDataType(VTK_FLOAT);
    }
  }
  else if (this->OutputPointsPrecision == vtkAlgorithm::SINGLE_PRECISION)
  {
    newPoints->SetDataType(VTK_FLOAT);
  }
  else if (this->OutputPointsPrecision == vtkAlgorithm::DOUBLE_PRECISION)
  {
    newPoints->SetDataType(VTK_DOUBLE);
  }

  newPoints->Allocate(numPts);

  vtkSmartPointer<vtkIdList> pointMap =
    vtkSmartPointer<vtkIdList>::Take(vtkIdList::New()); // maps old point ids into new
  pointMap->SetNumberOfIds(numPts);
  for (vtkIdType i = 0; i < numPts; i++)
  {
    pointMap->SetId(i, -1);
  }

  vtkSmartPointer<vtkIdList> newCellPts = vtkSmartPointer<vtkIdList>::Take(vtkIdList::New());

  // are we using pointScalars?
  int fieldAssociation = this->GetInputArrayAssociation(0, inputVector);
  bool usePointScalars = fieldAssociation == vtkDataObject::FIELD_ASSOCIATION_POINTS;

  vtkUnsignedCharArray* ghosts = input->GetCellData()->GetGhostArray();

  // Check that the scalars of each cell satisfy the threshold criterion
  vtkSmartPointer<vtkCellIterator> it =
    vtkSmartPointer<vtkCellIterator>::Take(input->NewCellIterator());
  vtkIdType numberOfCells = input->GetNumberOfCells();
  vtkIdType index = 0;
  const vtkIdType tenth = numberOfCells / 10 + 1;
  bool abort = false;
  for (it->InitTraversal(); !it->IsDoneWithTraversal() && !abort; it->GoToNextCell())
  {
    if (index % tenth == 0)
    {
      this->UpdateProgress(index * 1.0 / numberOfCells);
      abort = this->CheckAbort();
    }
    if (ghosts && ghosts->GetValue(index++) & vtkDataSetAttributes::HIDDENCELL)
    {
      continue;
    }

    int cellType = it->GetCellType();
    if (cellType == VTK_EMPTY_CELL)
    {
      continue;
    }

    vtkIdType cellId = it->GetCellId();
    vtkIdList* cellPts = it->GetPointIds();
    int numCellPts = it->GetNumberOfPoints();

    int keepCell(0);
    if (usePointScalars)
    {
      if (this->AllScalars)
      {
        keepCell = 1;
        for (int i = 0; keepCell && (i < numCellPts); i++)
        {
          vtkIdType ptId = cellPts->GetId(i);
          keepCell = this->EvaluateComponents(inScalars, ptId);
        }
      }
      else
      {
        if (!this->UseContinuousCellRange)
        {
          keepCell = 0;
          for (int i = 0; (!keepCell) && (i < numCellPts); i++)
          {
            vtkIdType ptId = cellPts->GetId(i);
            keepCell = this->EvaluateComponents(inScalars, ptId);
          }
        }
        else
        {
          keepCell = this->EvaluateCell(inScalars, cellPts, numCellPts);
        }
      }
    }
    else // use cell scalars
    {
      keepCell = this->EvaluateComponents(inScalars, cellId);
    }

    // Invert the keep flag if the Invert option is enabled.
    keepCell = this->Invert ? (1 - keepCell) : keepCell;

    if (numCellPts > 0 && keepCell)
    {
      // satisfied thresholding (also non-empty cell, i.e. not VTK_EMPTY_CELL)
      for (vtkIdType i = 0; i < numCellPts; i++)
      {
        vtkIdType ptId = cellPts->GetId(i);
        vtkIdType newId = pointMap->GetId(ptId);
        if (newId < 0)
        {
          double x[3];
          input->GetPoint(ptId, x);
          newId = newPoints->InsertNextPoint(x);
          pointMap->SetId(ptId, newId);
          outPD->CopyData(pd, ptId, newId);
        }
        newCellPts->InsertId(i, newId);
      }
      // special handling for polyhedron cells
      if (cellType == VTK_POLYHEDRON)
      {
        newCellPts->Reset();
        vtkIdList* faces = it->GetFaces();
        for (vtkIdType j = 0; j < faces->GetNumberOfIds(); ++j)
        {
          newCellPts->InsertNextId(faces->GetId(j));
        }
        vtkUnstructuredGrid::ConvertFaceStreamPointIds(newCellPts, pointMap->GetPointer(0));
      }
      vtkIdType newCellId = output->InsertNextCell(it->GetCellType(), newCellPts);
      outCD->CopyData(cd, cellId, newCellId);
      newCellPts->Reset();
    } // satisfied thresholding
  }   // for all cells

  vtkDebugMacro(<< "Extracted " << output->GetNumberOfCells() << " number of cells.");

  // now  update ourselves
  output->SetPoints(newPoints);
  output->Squeeze();

  return 1;
}

int vtkThreshold::EvaluateCell(vtkDataArray* scalars, vtkIdList* cellPts, int numCellPts)
{
  int c(0);
  int numComp = scalars->GetNumberOfComponents();
  int keepCell(0);
  switch (this->ComponentMode)
  {
    case VTK_COMPONENT_MODE_USE_SELECTED:
      c = (this->SelectedComponent < numComp) ? (this->SelectedComponent) : (0);
      keepCell = EvaluateCell(scalars, c, cellPts, numCellPts);
      break;
    case VTK_COMPONENT_MODE_USE_ANY:
      keepCell = 0;
      for (c = 0; (!keepCell) && (c < numComp); c++)
      {
        keepCell = EvaluateCell(scalars, c, cellPts, numCellPts);
      }
      break;
    case VTK_COMPONENT_MODE_USE_ALL:
      keepCell = 1;
      for (c = 0; keepCell && (c < numComp); c++)
      {
        keepCell = EvaluateCell(scalars, c, cellPts, numCellPts);
      }
      break;
  }
  return keepCell;
}

int vtkThreshold::EvaluateCell(vtkDataArray* scalars, int c, vtkIdList* cellPts, int numCellPts)
{
  double minScalar = DBL_MAX, maxScalar = DBL_MIN;
  for (int i = 0; i < numCellPts; i++)
  {
    vtkIdType ptId = cellPts->GetId(i);
    double s = scalars->GetComponent(ptId, c);
    minScalar = std::min(s, minScalar);
    maxScalar = std::max(s, maxScalar);
  }

  int keepCell = !(this->LowerThreshold > maxScalar || this->UpperThreshold < minScalar);
  return keepCell;
}

int vtkThreshold::EvaluateComponents(vtkDataArray* scalars, vtkIdType id)
{
  int keepCell = 0;
  int numComp = scalars->GetNumberOfComponents();
  int c;

  switch (this->ComponentMode)
  {
    case VTK_COMPONENT_MODE_USE_SELECTED:
      c = (this->SelectedComponent < numComp) ? (this->SelectedComponent) : (0);
      keepCell = (this->*(this->ThresholdFunction))(scalars->GetComponent(id, c));
      break;
    case VTK_COMPONENT_MODE_USE_ANY:
      keepCell = 0;
      for (c = 0; (!keepCell) && (c < numComp); c++)
      {
        keepCell = (this->*(this->ThresholdFunction))(scalars->GetComponent(id, c));
      }
      break;
    case VTK_COMPONENT_MODE_USE_ALL:
      keepCell = 1;
      for (c = 0; keepCell && (c < numComp); c++)
      {
        keepCell = (this->*(this->ThresholdFunction))(scalars->GetComponent(id, c));
      }
      break;
  }
  return keepCell;
}

// Return the method for manipulating scalar data as a string.
const char* vtkThreshold::GetAttributeModeAsString()
{
  if (this->AttributeMode == VTK_ATTRIBUTE_MODE_DEFAULT)
  {
    return "Default";
  }
  else if (this->AttributeMode == VTK_ATTRIBUTE_MODE_USE_POINT_DATA)
  {
    return "UsePointData";
  }
  else
  {
    return "UseCellData";
  }
}

// Return a string representation of the component mode
const char* vtkThreshold::GetComponentModeAsString()
{
  if (this->ComponentMode == VTK_COMPONENT_MODE_USE_SELECTED)
  {
    return "UseSelected";
  }
  else if (this->ComponentMode == VTK_COMPONENT_MODE_USE_ANY)
  {
    return "UseAny";
  }
  else
  {
    return "UseAll";
  }
}

void vtkThreshold::SetPointsDataType(int type)
{
  if (type == VTK_FLOAT)
  {
    this->SetOutputPointsPrecision(SINGLE_PRECISION);
  }
  else if (type == VTK_DOUBLE)
  {
    this->SetOutputPointsPrecision(DOUBLE_PRECISION);
  }
}

int vtkThreshold::GetPointsDataType()
{
  if (this->OutputPointsPrecision == SINGLE_PRECISION)
  {
    return VTK_FLOAT;
  }
  else if (this->OutputPointsPrecision == DOUBLE_PRECISION)
  {
    return VTK_DOUBLE;
  }

  return 0;
}

void vtkThreshold::SetOutputPointsPrecision(int precision)
{
  if (this->OutputPointsPrecision != precision)
  {
    this->OutputPointsPrecision = precision;
    this->Modified();
  }
}

int vtkThreshold::GetOutputPointsPrecision() const
{
  return this->OutputPointsPrecision;
}

int vtkThreshold::FillInputPortInformation(int, vtkInformation* info)
{
  info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
  return 1;
}

void vtkThreshold::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Attribute Mode: " << this->GetAttributeModeAsString() << endl;
  os << indent << "Component Mode: " << this->GetComponentModeAsString() << endl;
  os << indent << "Selected Component: " << this->SelectedComponent << endl;

  os << indent << "All Scalars: " << this->AllScalars << "\n";
  if (this->ThresholdFunction == &vtkThreshold::Upper)
  {
    os << indent << "Threshold By Upper\n";
  }

  else if (this->ThresholdFunction == &vtkThreshold::Lower)
  {
    os << indent << "Threshold By Lower\n";
  }

  else if (this->ThresholdFunction == &vtkThreshold::Between)
  {
    os << indent << "Threshold Between\n";
  }

  os << indent << "Lower Threshold: " << this->LowerThreshold << "\n";
  os << indent << "Upper Threshold: " << this->UpperThreshold << "\n";
  os << indent << "Precision of the output points: " << this->OutputPointsPrecision << "\n";
  os << indent << "Use Continuous Cell Range: " << this->UseContinuousCellRange << endl;
}
VTK_ABI_NAMESPACE_END

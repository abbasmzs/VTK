/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkImageDataGeometryFilter.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkImageDataGeometryFilter.h"

#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkImageData.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkImageDataGeometryFilter);

// Construct with initial extent of all the data
vtkImageDataGeometryFilter::vtkImageDataGeometryFilter()
{
  this->Extent[0] = 0;
  this->Extent[1] = VTK_INT_MAX;
  this->Extent[2] = 0;
  this->Extent[3] = VTK_INT_MAX;
  this->Extent[4] = 0;
  this->Extent[5] = VTK_INT_MAX;
  this->ThresholdCells = 0;
  this->ThresholdValue = 0.0;
  this->OutputTriangles = 0;
}

int vtkImageDataGeometryFilter::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // get the info objects
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  vtkInformation* outInfo = outputVector->GetInformationObject(0);

  // get the input and output
  vtkImageData* input = vtkImageData::SafeDownCast(inInfo->Get(vtkDataObject::DATA_OBJECT()));
  vtkPolyData* output = vtkPolyData::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));

  int *dims, dimension, dir[3], diff[3];
  int i, j, k, extent[6], s, threshok;
  vtkIdType idx, startIdx, startCellIdx;
  vtkIdType ptIds[4], cellId, triIds[3];
  vtkPoints* newPts = nullptr;
  vtkCellArray* newVerts = nullptr;
  vtkCellArray* newLines = nullptr;
  vtkCellArray* newPolys = nullptr;
  vtkIdType totPoints, pos;
  int offset[3], numPolys;
  double x[3];
  vtkPointData *pd, *outPD;
  vtkDataArray* pointScalars;
  vtkCellData *cd, *outCD;
  vtkDebugMacro(<< "Extracting structured points geometry");

  pd = input->GetPointData();
  outPD = output->GetPointData();
  cd = input->GetCellData();
  outCD = output->GetCellData();
  pointScalars = pd->GetScalars();
  //  this->PointData.CopyNormalsOff();
  dims = input->GetDimensions();

  if (dims[0] <= 0 || dims[1] <= 0 || dims[2] <= 0)
  {
    return 1;
  }

  //
  // Based on the dimensions of the structured data, and the extent of the geometry,
  // compute the combined extent plus the dimensionality of the data
  //
  for (dimension = 3, i = 0; i < 3; i++)
  {
    extent[2 * i] = this->Extent[2 * i] < 0 ? 0 : this->Extent[2 * i];
    extent[2 * i] = this->Extent[2 * i] >= dims[i] ? dims[i] - 1 : this->Extent[2 * i];
    extent[2 * i + 1] = this->Extent[2 * i + 1] >= dims[i] ? dims[i] - 1 : this->Extent[2 * i + 1];
    if (extent[2 * i + 1] < extent[2 * i])
    {
      extent[2 * i + 1] = extent[2 * i];
    }
    if ((extent[2 * i + 1] - extent[2 * i]) == 0)
    {
      dimension--;
    }
  }
  //
  // Now create polygonal data based on dimension of data
  //
  startIdx = extent[0] + extent[2] * dims[0] + extent[4] * dims[0] * dims[1];
  // The cell index is a bit more complicated at the boundaries
  if (dims[0] == 1)
  {
    startCellIdx = extent[0];
  }
  else
  {
    startCellIdx = (extent[0] < dims[0] - 1) ? extent[0] : extent[0] - 1;
  }
  if (dims[1] == 1)
  {
    startCellIdx += extent[2] * (dims[0] - 1);
  }
  else
  {
    startCellIdx +=
      (extent[2] < dims[1] - 1) ? extent[2] * (dims[0] - 1) : (extent[2] - 1) * (dims[0] - 1);
  }
  if (dims[2] == 1)
  {
    startCellIdx += extent[4] * (dims[0] - 1) * (dims[1] - 1);
  }
  else
  {
    startCellIdx += (extent[4] < dims[2] - 1) ? extent[4] * (dims[0] - 1) * (dims[1] - 1)
                                              : (extent[4] - 1) * (dims[0] - 1) * (dims[1] - 1);
  }

  switch (dimension)
  {
    default:
      break;

    case 0: // --------------------- build point -----------------------

      newPts = vtkPoints::New();
      newPts->Allocate(1);
      newVerts = vtkCellArray::New();
      newVerts->AllocateEstimate(1, 1);
      outPD->CopyAllocate(pd, 1);
      outCD->CopyAllocate(cd, 1);

      ptIds[0] = newPts->InsertNextPoint(input->GetPoint(startIdx));
      outPD->CopyData(pd, startIdx, ptIds[0]);

      cellId = newVerts->InsertNextCell(1, ptIds);
      outCD->CopyData(cd, startIdx, cellId);
      break;

    case 1: // --------------------- build line -----------------------

      for (dir[0] = dir[1] = dir[2] = totPoints = 0, i = 0; i < 3; i++)
      {
        if ((diff[i] = extent[2 * i + 1] - extent[2 * i]) > 0)
        {
          dir[0] = i;
          totPoints = diff[i] + 1;
          break;
        }
      }
      newPts = vtkPoints::New();
      newPts->Allocate(totPoints);
      newLines = vtkCellArray::New();
      newLines->AllocateEstimate(totPoints - 1, 2);
      outPD->CopyAllocate(pd, totPoints);
      outCD->CopyAllocate(cd, totPoints - 1);
      //
      //  Load data
      //
      if (dir[0] == 0)
      {
        offset[0] = 1;
      }
      else if (dir[0] == 1)
      {
        offset[0] = dims[0];
      }
      else
      {
        offset[0] = dims[0] * dims[1];
      }

      for (i = 0; i < totPoints; i++)
      {
        idx = startIdx + i * offset[0];
        input->GetPoint(idx, x);
        ptIds[0] = newPts->InsertNextPoint(x);
        outPD->CopyData(pd, idx, ptIds[0]);
      }

      if (dir[0] == 0)
      {
        offset[0] = 1;
      }
      else if (dir[0] == 1)
      {
        offset[0] = dims[0] - 1;
      }
      else
      {
        offset[0] = (dims[0] - 1) * (dims[1] - 1);
      }

      for (i = 0; i < (totPoints - 1); i++)
      {
        idx = startCellIdx + i * offset[0];
        ptIds[0] = i;
        ptIds[1] = i + 1;
        cellId = newLines->InsertNextCell(2, ptIds);
        outCD->CopyData(cd, idx, cellId);
      }
      break;

    case 2: // --------------------- build plane -----------------------
            //
            //  Create the data objects
            //
      for (dir[0] = dir[1] = dir[2] = idx = 0, i = 0; i < 3; i++)
      {
        if ((diff[i] = extent[2 * i + 1] - extent[2 * i]) != 0)
        {
          dir[idx++] = i;
        }
        else
        {
          dir[2] = i;
        }
      }

      totPoints = (diff[dir[0]] + 1) * (diff[dir[1]] + 1);
      numPolys = diff[dir[0]] * diff[dir[1]];

      newPts = vtkPoints::New();
      newPts->Allocate(totPoints);
      newPolys = vtkCellArray::New();
      if (this->OutputTriangles)
      {
        newPolys->AllocateEstimate(2 * numPolys, 3);
      }
      else
      {
        newPolys->AllocateEstimate(numPolys, 4);
      }
      outPD->CopyAllocate(pd, totPoints);
      outCD->CopyAllocate(cd, numPolys);
      //
      //  Create vertices
      //
      for (i = 0; i < 2; i++)
      {
        if (dir[i] == 0)
        {
          offset[i] = 1;
        }
        else if (dir[i] == 1)
        {
          offset[i] = dims[0];
        }
        else if (dir[i] == 2)
        {
          offset[i] = dims[0] * dims[1];
        }
      }

      for (pos = startIdx, j = 0; j < (diff[dir[1]] + 1); j++)
      {
        for (i = 0; i < (diff[dir[0]] + 1); i++)
        {
          idx = pos + i * offset[0];
          input->GetPoint(idx, x);
          ptIds[0] = newPts->InsertNextPoint(x);
          outPD->CopyData(pd, idx, ptIds[0]);
        }
        pos += offset[1];
      }

      //
      //  Create cells
      //
      for (i = 0; i < 2; i++)
      {
        if (dir[i] == 0)
        {
          offset[i] = 1;
        }
        else if (dir[i] == 1)
        {
          offset[i] = (dims[0] - 1);
        }
        else if (dir[i] == 2)
        {
          offset[i] = (dims[0] - 1) * (dims[1] - 1);
        }
      }

      for (pos = startCellIdx, j = 0; j < diff[dir[1]]; j++)
      {
        for (i = 0; i < diff[dir[0]]; i++)
        {
          idx = pos + i * offset[0];
          ptIds[0] = i + j * (diff[dir[0]] + 1);
          ptIds[1] = ptIds[0] + 1;
          ptIds[2] = ptIds[1] + diff[dir[0]] + 1;
          ptIds[3] = ptIds[2] - 1;
          if (this->ThresholdCells)
          {
            threshok = false;
            for (s = 0; !threshok && s < 4; s++)
            {
              if (pointScalars->GetComponent(ptIds[s], 0) > this->ThresholdValue)
              {
                threshok = true;
                break;
              }
            }
            if (threshok)
            {
              if (this->OutputTriangles)
              {
                triIds[0] = ptIds[0];
                triIds[1] = ptIds[2];
                triIds[2] = ptIds[3];
                cellId = newPolys->InsertNextCell(3, ptIds);
                outCD->CopyData(cd, idx, cellId);
                cellId = newPolys->InsertNextCell(3, triIds);
                outCD->CopyData(cd, idx, cellId);
              }
              else
              {
                cellId = newPolys->InsertNextCell(4, ptIds);
                outCD->CopyData(cd, idx, cellId);
              }
            }
          }
          else
          {
            cellId = newPolys->InsertNextCell(4, ptIds);
            outCD->CopyData(cd, idx, cellId);
          }
        }
        pos += offset[1];
      }
      break;

    case 3: // ------------------- grab points in volume  --------------

      //
      // Create data objects
      //
      for (i = 0; i < 3; i++)
      {
        diff[i] = extent[2 * i + 1] - extent[2 * i];
      }
      totPoints = (diff[0] + 1) * (diff[1] + 1) * (diff[2] + 1);

      newPts = vtkPoints::New();
      newPts->Allocate(totPoints);
      newVerts = vtkCellArray::New();
      newVerts->AllocateEstimate(totPoints, 1);
      outPD->CopyAllocate(pd, totPoints);
      outCD->CopyAllocate(cd, totPoints);
      //
      // Create vertices and cells
      //
      offset[0] = dims[0];
      offset[1] = dims[0] * dims[1];

      for (k = 0; k < (diff[2] + 1); k++)
      {
        for (j = 0; j < (diff[1] + 1); j++)
        {
          pos = startIdx + j * offset[0] + k * offset[1];
          for (i = 0; i < (diff[0] + 1); i++)
          {
            input->GetPoint(pos + i, x);
            ptIds[0] = newPts->InsertNextPoint(x);
            outPD->CopyData(pd, pos + i, ptIds[0]);
            cellId = newVerts->InsertNextCell(1, ptIds);
            outCD->CopyData(cd, pos + i, cellId);
          }
        }
      }
      break; /* end this case */

  } // switch
    //
    // Update self and release memory
    //
  if (newPts)
  {
    output->SetPoints(newPts);
    newPts->Delete();
  }

  if (newVerts)
  {
    output->SetVerts(newVerts);
    newVerts->Delete();
  }

  if (newLines)
  {
    output->SetLines(newLines);
    newLines->Delete();
  }

  if (newPolys)
  {
    output->SetPolys(newPolys);
    newPolys->Delete();
  }

  return 1;
}

void vtkImageDataGeometryFilter::SetExtent(
  int iMin, int iMax, int jMin, int jMax, int kMin, int kMax)
{
  int extent[6];

  extent[0] = iMin;
  extent[1] = iMax;
  extent[2] = jMin;
  extent[3] = jMax;
  extent[4] = kMin;
  extent[5] = kMax;

  this->SetExtent(extent);
}

// Specify (imin,imax, jmin,jmax, kmin,kmax) indices.
void vtkImageDataGeometryFilter::SetExtent(int extent[6])
{
  int i;

  if (extent[0] != this->Extent[0] || extent[1] != this->Extent[1] ||
    extent[2] != this->Extent[2] || extent[3] != this->Extent[3] || extent[4] != this->Extent[4] ||
    extent[5] != this->Extent[5])
  {
    this->Modified();
    for (i = 0; i < 3; i++)
    {
      if (extent[2 * i] < 0)
      {
        extent[2 * i] = 0;
      }
      if (extent[2 * i + 1] < extent[2 * i])
      {
        extent[2 * i + 1] = extent[2 * i];
      }
      this->Extent[2 * i] = extent[2 * i];
      this->Extent[2 * i + 1] = extent[2 * i + 1];
    }
  }
}

int vtkImageDataGeometryFilter::FillInputPortInformation(int, vtkInformation* info)
{
  info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkImageData");
  return 1;
}

void vtkImageDataGeometryFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Extent: \n";
  os << indent << "  Jmin,Jmax: (" << this->Extent[2] << ", " << this->Extent[3] << ")\n";
  os << indent << "  Kmin,Kmax: (" << this->Extent[4] << ", " << this->Extent[5] << ")\n";
  os << indent << "  Imin,Imax: (" << this->Extent[0] << ", " << this->Extent[1] << ")\n";
  os << indent << "OutputTriangles " << this->OutputTriangles << "\n";
  os << indent << "ThresholdValue " << this->ThresholdValue << "\n";
  os << indent << "ThresholdCells " << this->ThresholdCells << "\n";
}
VTK_ABI_NAMESPACE_END

/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkProp3DCollection.h

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/**
 * @class   vtkProp3DCollection
 * @brief   an ordered list of 3D props
 *
 * vtkProp3DCollection represents and provides methods to manipulate a list of
 * 3D props (i.e., vtkProp3D and subclasses). The list is ordered and
 * duplicate entries are not prevented.
 *
 * @sa
 * vtkProp3D vtkCollection
 */

#ifndef vtkProp3DCollection_h
#define vtkProp3DCollection_h

#include "vtkProp3D.h" // Needed for inline methods
#include "vtkPropCollection.h"
#include "vtkRenderingCoreModule.h" // For export macro

VTK_ABI_NAMESPACE_BEGIN
class VTKRENDERINGCORE_EXPORT vtkProp3DCollection : public vtkPropCollection
{
public:
  static vtkProp3DCollection* New();
  vtkTypeMacro(vtkProp3DCollection, vtkPropCollection);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * Add an actor to the bottom of the list.
   */
  void AddItem(vtkProp3D* p);

  /**
   * Get the next actor in the list.
   */
  vtkProp3D* GetNextProp3D();

  /**
   * Get the last actor in the list.
   */
  vtkProp3D* GetLastProp3D();

  /**
   * Reentrant safe way to get an object in a collection. Just pass the
   * same cookie back and forth.
   */
  vtkProp3D* GetNextProp3D(vtkCollectionSimpleIterator& cookie)
  {
    return static_cast<vtkProp3D*>(this->GetNextItemAsObject(cookie));
  }

protected:
  vtkProp3DCollection() = default;
  ~vtkProp3DCollection() override = default;

private:
  // hide the standard AddItem from the user and the compiler.
  void AddItem(vtkObject* o) { this->vtkCollection::AddItem(o); }
  void AddItem(vtkProp* o) { this->vtkPropCollection::AddItem(o); }

private:
  vtkProp3DCollection(const vtkProp3DCollection&) = delete;
  void operator=(const vtkProp3DCollection&) = delete;
};

inline void vtkProp3DCollection::AddItem(vtkProp3D* a)
{
  this->vtkCollection::AddItem(a);
}

inline vtkProp3D* vtkProp3DCollection::GetNextProp3D()
{
  return static_cast<vtkProp3D*>(this->GetNextItemAsObject());
}

inline vtkProp3D* vtkProp3DCollection::GetLastProp3D()
{
  if (this->Bottom == nullptr)
  {
    return nullptr;
  }
  else
  {
    return static_cast<vtkProp3D*>(this->Bottom->Item);
  }
}

VTK_ABI_NAMESPACE_END
#endif

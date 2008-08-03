/*=========================================================================

  Program:   ITK-SNAP
  Module:    $RCSfile: GenericImageData.h,v $
  Language:  C++
  Date:      $Date: 2007/12/30 04:43:03 $
  Version:   $Revision: 1.2 $
  Copyright (c) 2007 Paul A. Yushkevich
  
  This file is part of ITK-SNAP 

  ITK-SNAP is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details. 
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  -----

  Copyright (c) 2003 Insight Software Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

  This software is distributed WITHOUT ANY WARRANTY; without even
  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the above copyright notices for more information.

  -----

  Copyright (c) 2003 Insight Software Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

  This software is distributed WITHOUT ANY WARRANTY; without even
  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the above copyright notices for more information. 

=========================================================================*/
#ifndef __GenericImageData_h_
#define __GenericImageData_h_

#include "SNAPCommon.h"
#include "IRISException.h"
#include "LabelImageWrapper.h"
#include "GreyImageWrapper.h"
#include "RGBImageWrapper.h"
#include "GlobalState.h"
#include "ImageCoordinateGeometry.h"

class IRISApplication;

/**
 * \class GenericImageData
 * \brief This class encapsulates the image data used by 
 * the IRIS component of SnAP.  
 *
 * This data consists of a grey image [gi] and a segmentation image [si].
 * The following rules must be satisfied by this class:
 *  + exists(si) ==> exists(gi)
 *  + if exists(si) then size(si) == size(gi)
 */
class GenericImageData 
{
public:
  // Image type definitions
  typedef GreyImageWrapper::ImageType GreyImageType;
  typedef RGBImageWrapper::ImageType RGBImageType;
  typedef LabelImageWrapper::ImageType LabelImageType;
  typedef itk::ImageRegion<3> RegionType;
  typedef itk::Image<float,3> FloatImageType;

  GenericImageData(IRISApplication *parent);
  virtual ~GenericImageData() {};


  /**
   * Access the greyscale image (read only access is allowed)
   */
  GreyImageWrapper* GetGrey() {
    assert(m_GreyWrapper.IsInitialized());
    return &m_GreyWrapper;
  }

  /**
   * Access the RGB image (read only access is allowed)
   */
  RGBImageWrapper* GetRGB() {
    assert(m_GreyWrapper.IsInitialized() && m_RGBWrapper.IsInitialized());
    return &m_RGBWrapper;
  }

  /**
   * Access the segmentation image (read only access allowed 
   * to preserve state)
   */
  LabelImageWrapper* GetSegmentation() {
    assert(m_GreyWrapper.IsInitialized() && m_LabelWrapper.IsInitialized());
    return &m_LabelWrapper;
  }

  /** 
   * Get the extents of the image volume
   */
  Vector3ui GetVolumeExtents() const {
    assert(m_GreyWrapper.IsInitialized());
    assert(m_GreyWrapper.GetSize() == m_Size);
    return m_Size;
  }

  /** 
   * Get the ImageRegion (largest possible region of all the images)
   */
  RegionType GetImageRegion() const;

  /**
   * Get the spacing of the gray scale image (and all the associated images) 
   */
  Vector3d GetImageSpacing();

  /**
   * Get the origin of the gray scale image (and all the associated images) 
   */
  Vector3d GetImageOrigin();

  /**
   * Set the grey image (read important note).
   * 
   * Note: this method replaces the internal pointer to the grey image
   * by the pointer that is passed in.  That means that the caller should relinquish
   * control of this pointer and that the GenericImageData class will dispose of the
   * pointer properly. 
   *
   * The second parameter to this method is the new geometry object, which depends
   * on the size of the grey image and will be updated.
   */
  virtual void SetGreyImage(GreyImageType *newGreyImage,
                    const ImageCoordinateGeometry &newGeometry);
  /*
   * We also have the option of sending in a float image. This will get
   * scaled and quantized before being set as the grey image.
   */
  virtual void SetGreyImage(FloatImageType *newGreyImage,
                    const ImageCoordinateGeometry &newGeometry);

  virtual void SetRGBImage(RGBImageType *newRGBImage,
                    const ImageCoordinateGeometry &newGeometry);
  
  virtual void SetRGBImage(RGBImageType *newRGBImage);
  
  /**
   * This method sets the segmentation image (see note for SetGrey).
   */
  virtual void SetSegmentationImage(LabelImageType *newLabelImage);

  /**
   * Set voxel in segmentation image
   */
  void SetSegmentationVoxel(const Vector3ui &index, LabelType value);

  /**
   * Check validity of greyscale image
   */
  bool IsGreyLoaded();

  /**
   * Check validity of RGB image
   */
  bool IsRGBLoaded();

  /**
   * Check validity of segmentation image
   */
  bool IsSegmentationLoaded();

  /**
   * Set the cursor (crosshairs) position, in pixel coordinates
   */
  virtual void SetCrosshairs(const Vector3ui &crosshairs);

  /**
   * Set the image coordinate geometry for this image set.  Propagates
   * the transform to the internal image wrappers
   */
  virtual void SetImageGeometry(const ImageCoordinateGeometry &geometry);

  /** Get the image coordinate geometry */
  irisGetMacro(ImageGeometry,ImageCoordinateGeometry);

protected:
  // Wrapper around the grey-scale image
  GreyImageWrapper m_GreyWrapper;

  // Wrapper around the RGB image
  RGBImageWrapper m_RGBWrapper;

  // Wrapper around the segmentatoin image
  LabelImageWrapper m_LabelWrapper;

  // A list of linked wrappers, whose cursor position and image geometry
  // are updated concurrently
  std::list<ImageWrapperBase *> m_LinkedWrappers;

  // Dimensions of the images (must match) 
  Vector3ui m_Size;

  // Parent object
  IRISApplication *m_Parent;

  // Image coordinate geometry (it's placed here because the transform depends
  // on image size)
  ImageCoordinateGeometry m_ImageGeometry;
};

#endif

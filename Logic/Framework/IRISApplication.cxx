/*=========================================================================

  Program:   ITK-SNAP
  Module:    $RCSfile: IRISApplication.cxx,v $
  Language:  C++
  Date:      $Date: 2008/03/25 19:31:31 $
  Version:   $Revision: 1.10 $
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

=========================================================================*/
// Borland compiler is very lazy so we need to instantiate the template
//  by hand 
#if defined(__BORLANDC__)
#include "SNAPBorlandDummyTypes.h"
#endif

#include "IRISApplication.h"

#include "GlobalState.h"
#include "GuidedImageIO.h"
#include "IRISImageData.h"
#include "IRISVectorTypesToITKConversion.h"
#include "SNAPImageData.h"
#include "MeshObject.h"
#include "MeshExportSettings.h"
#include "IntensityCurveVTK.h"
#include "itkImageRegionIterator.h"
#include "itkImageRegionConstIterator.h"
#include "itkImageRegionIteratorWithIndex.h"
#include "itkPasteImageFilter.h"
#include "itkImageRegionIterator.h"
#include "itkIdentityTransform.h"
#include "itkResampleImageFilter.h"
#include "itkNearestNeighborInterpolateImageFunction.h"
#include "itkBSplineInterpolateImageFunction.h"
#include "itkLinearInterpolateImageFunction.h"
#include "itkWindowedSincInterpolateImageFunction.h"
#include "itkImageFileWriter.h"
#include "itkFlipImageFilter.h"

#include "IRISSlicer.h"


#include <stdio.h>
#include <sstream>
#include <iomanip>

using namespace itk;

IRISApplication
::IRISApplication() 
: m_UndoManager(4,200000)
{
  // Construct new global state object
  m_GlobalState = new GlobalState;

  // Create a new system interface
  m_SystemInterface = new SystemInterface();

  // Initialize the color table
  m_ColorLabelTable = new ColorLabelTable();

  // Contruct the IRIS and SNAP data objects
  m_IRISImageData = new IRISImageData(this);
  m_SNAPImageData = NULL;

  // Set the current IRIS pointer
  m_CurrentImageData = m_IRISImageData;

  // Construct a new intensity curve
  m_IntensityCurve = IntensityCurveVTK::New();
  m_IntensityCurve->Initialize(3);

  // Initialize the image-anatomy transformation
  m_ImageToAnatomyRAI = "LPI";

  // Initialize the display-anatomy transformation
  m_DisplayToAnatomyRAI[0] = "LPS";
  m_DisplayToAnatomyRAI[1] = "AIR";
  m_DisplayToAnatomyRAI[2] = "LIP";
}


const char *
IRISApplication::
GetImageToAnatomyRAI()
{
  return m_ImageToAnatomyRAI.c_str();
}


const char *
IRISApplication::
GetDisplayToAnatomyRAI(unsigned int slice)
{
  return m_DisplayToAnatomyRAI[slice].c_str();
}


IRISApplication
::~IRISApplication() 
{
  m_IntensityCurve = NULL;

  delete m_IRISImageData;
  if(m_SNAPImageData)
    delete m_SNAPImageData;
  delete m_GlobalState;
  delete m_ColorLabelTable;
  delete m_SystemInterface;
}

void 
IRISApplication
::InitializeSNAPImageData(const SNAPSegmentationROISettings &roi,
                          CommandType *progressCommand) 
{
  assert(m_SNAPImageData == NULL);

  // Create the SNAP image data object
  m_SNAPImageData = new SNAPImageData(this);

  // Get the roi chunk from the grey image
  GreyImageType::Pointer imgNewGrey = 
    m_IRISImageData->GetGrey()->DeepCopyRegion(roi,progressCommand);

  // Get the size of the region
  Vector3ui size = to_unsigned_int(
    Vector3ul(imgNewGrey->GetLargestPossibleRegion().GetSize().GetSize()));

  // Compute an image coordinate geometry for the region of interest
  ImageCoordinateGeometry icg(m_ImageToAnatomyRAI,m_DisplayToAnatomyRAI,size);

  // Assign the new wrapper to the target
  m_SNAPImageData->SetGreyImage(imgNewGrey,icg);
  
  // Override the interpolator in ROI for label interpolation, or we will get
  // nonsense
  SNAPSegmentationROISettings roiLabel = roi;
  roiLabel.SetInterpolationMethod(SNAPSegmentationROISettings::NEAREST_NEIGHBOR);

  // Get chunk of the label image
  LabelImageType::Pointer imgNewLabel = 
    m_IRISImageData->GetSegmentation()->DeepCopyRegion(roiLabel,progressCommand);

  // Filter the segmentation image to only allow voxels of 0 intensity and 
  // of the current drawing color
  LabelType passThroughLabel = m_GlobalState->GetDrawingColorLabel();

  typedef ImageRegionIterator<LabelImageType> IteratorType;  
  IteratorType itLabel(imgNewLabel,imgNewLabel->GetBufferedRegion());  
  while(!itLabel.IsAtEnd())
    {
    if(itLabel.Value() != passThroughLabel)
      itLabel.Value() = (LabelType) 0;
    ++itLabel;
    }

  // Pass the cleaned up segmentation image to SNAP
  m_SNAPImageData->SetSegmentationImage(imgNewLabel);

  // Pass the label description of the drawing label to the SNAP image data
  m_SNAPImageData->SetColorLabel(
    m_ColorLabelTable->GetColorLabel(passThroughLabel));

  // Assign the intensity mapping function to the Snap data
  m_SNAPImageData->GetGrey()->SetIntensityMapFunction(m_IntensityCurve);

  // Initialize the speed image of the SNAP image data
  m_SNAPImageData->InitializeSpeed();

  // Remember the ROI object
  m_GlobalState->SetSegmentationROISettings(roi);
}

void 
IRISApplication
::SetDisplayToAnatomyRAI(const char *rai0,const char *rai1,const char *rai2)
{
  // Store the new RAI code
  m_DisplayToAnatomyRAI[0] = rai0;
  m_DisplayToAnatomyRAI[1] = rai1;
  m_DisplayToAnatomyRAI[2] = rai2;

  if(!m_IRISImageData->IsGreyLoaded()) 
    return;
  
  // Create the appropriate transform and pass it to the IRIS data
  m_IRISImageData->SetImageGeometry(
    ImageCoordinateGeometry(
      m_ImageToAnatomyRAI,
      m_DisplayToAnatomyRAI,
      m_IRISImageData->GetVolumeExtents()));

  // Do the same for the SNAP data if needed
  if(!m_SNAPImageData)
    return;

  // Create the appropriate transform and pass it to the SNAP data
  m_SNAPImageData->SetImageGeometry(
    ImageCoordinateGeometry(
      m_ImageToAnatomyRAI,
      m_DisplayToAnatomyRAI,
      m_SNAPImageData->GetVolumeExtents()));
}

void 
IRISApplication
::UpdateIRISGreyImage(GreyLoadImageType *newGreyImage, 
                      const char *newImageRAI)
{
  // This has to happen in 'pure' IRIS mode
  assert(m_SNAPImageData == NULL);

  // Get the size of the image as a vector of uint
  Vector3ui size = 
    to_unsigned_int(
      Vector3ul(
        newGreyImage->GetBufferedRegion().GetSize().GetSize()));
  
  // Store the new RAI code
  m_ImageToAnatomyRAI[0] = newImageRAI[0];
  m_ImageToAnatomyRAI[1] = newImageRAI[1];
  m_ImageToAnatomyRAI[2] = newImageRAI[2];

  // Compute the new image geometry for the IRIS data
  ImageCoordinateGeometry icg(m_ImageToAnatomyRAI,m_DisplayToAnatomyRAI,size);
  
  // Update the image information in m_Driver->GetCurrentImageData()
  m_IRISImageData->SetGreyImage(newGreyImage,icg);    
  
  // Reinitialize the intensity mapping curve 
  m_IntensityCurve->Initialize(3);

  // Update the new grey image wrapper with the intensity mapping curve
  m_IRISImageData->GetGrey()->SetIntensityMapFunction(m_IntensityCurve);

  // Update the crosshairs position
  Vector3ui cursor = size;
  cursor /= 2;
  m_IRISImageData->SetCrosshairs(cursor);
  
  // TODO: Unify this!
  m_GlobalState->SetCrosshairsPosition(cursor) ;

  // Update the preprocessing settings in the global state
  m_GlobalState->SetEdgePreprocessingSettings(
    EdgePreprocessingSettings::MakeDefaultSettings());
  m_GlobalState->SetThresholdSettings(
    ThresholdSettings::MakeDefaultSettings(
      m_IRISImageData->GetGrey()));

  // Reset the UNDO manager
  m_UndoManager.Clear();
}

void 
IRISApplication
::UpdateIRISRGBImage(RGBImageType *newRGBImage, 
                      const char *newImageRAI)
{
  // This has to happen in 'pure' IRIS mode
  assert(m_SNAPImageData == NULL);

  // Get the size of the image as a vector of uint
  Vector3ui size = 
    to_unsigned_int(
      Vector3ul(
        newRGBImage->GetBufferedRegion().GetSize().GetSize()));
  
  // Store the new RAI code
  m_ImageToAnatomyRAI[0] = newImageRAI[0];
  m_ImageToAnatomyRAI[1] = newImageRAI[1];
  m_ImageToAnatomyRAI[2] = newImageRAI[2];

  // Compute the new image geometry for the IRIS data
  ImageCoordinateGeometry icg(m_ImageToAnatomyRAI,m_DisplayToAnatomyRAI,size);
  
  // Update the image information in m_Driver->GetCurrentImageData()
  m_IRISImageData->SetRGBImage(newRGBImage,icg);    
  
  // Reinitialize the intensity mapping curve 
  m_IntensityCurve->Initialize(3);

  // Update the new grey image wrapper with the intensity mapping curve
  m_IRISImageData->GetGrey()->SetIntensityMapFunction(m_IntensityCurve);

  // Update the crosshairs position
  Vector3ui cursor = size;
  cursor /= 2;
  m_IRISImageData->SetCrosshairs(cursor);
  
  // TODO: Unify this!
  m_GlobalState->SetCrosshairsPosition(cursor) ;

  // Update the preprocessing settings in the global state
  m_GlobalState->SetEdgePreprocessingSettings(
    EdgePreprocessingSettings::MakeDefaultSettings());
  m_GlobalState->SetThresholdSettings(
    ThresholdSettings::MakeDefaultSettings(
      m_IRISImageData->GetGrey()));

  // Reset the UNDO manager
  m_UndoManager.Clear();
}

void 
IRISApplication
::UpdateSNAPSpeedImage(SpeedImageType *newSpeedImage, 
                       SnakeType snakeMode)
{
  // This has to happen in SNAP mode
  assert(m_SNAPImageData);

  // Make sure the dimensions of the speed image are appropriate
  assert(m_SNAPImageData->GetGrey()->GetImage()->GetBufferedRegion().GetSize()
    == newSpeedImage->GetBufferedRegion().GetSize());

  // Initialize the speed wrapper
  if(!m_SNAPImageData->IsSpeedLoaded())
    m_SNAPImageData->InitializeSpeed();
  
  // Send the speed image to the image data
  m_SNAPImageData->GetSpeed()->SetImage(newSpeedImage);

  // Save the snake mode 
  m_GlobalState->SetSnakeMode(snakeMode);

  // Set the speed as valid
  m_GlobalState->SetSpeedValid(true);

  // Set the snake state
  if(snakeMode == EDGE_SNAKE)
    {
    m_SNAPImageData->GetSpeed()->SetModeToEdgeSnake();
    }
  else
    {
    m_SNAPImageData->GetSpeed()->SetModeToInsideOutsideSnake();
    }
}

void
IRISApplication
::UpdateIRISRGBImage(RGBImageType *newRGBImage)
{
  // This has to happen in 'pure' IRIS mode
  assert(m_SNAPImageData == NULL);

  // Update the iris data
  m_IRISImageData->SetRGBImage(newRGBImage); 
}

void
IRISApplication
::ClearIRISSegmentationImage()
{
  // This has to happen in 'pure' IRIS mode
  assert(m_SNAPImageData == NULL);

  // Fill the image with blanks
  this->m_IRISImageData->GetSegmentation()->GetImage()->FillBuffer(0);
  this->m_IRISImageData->GetSegmentation()->GetImage()->Modified();
}

void
IRISApplication
::UpdateIRISSegmentationImage(LabelImageType *newSegmentationImage)
{
  // This has to happen in 'pure' IRIS mode
  assert(m_SNAPImageData == NULL);

  // Update the iris data
  m_IRISImageData->SetSegmentationImage(newSegmentationImage); 

  // Update the color labels, so that for every label in the image
  // there is a valid color label
  LabelImageWrapper::ConstIterator it = 
    m_IRISImageData->GetSegmentation()->GetImageConstIterator();
  for( ; !it.IsAtEnd(); ++it)
    if(!m_ColorLabelTable->IsColorLabelValid(it.Get()))
      m_ColorLabelTable->SetColorLabelValid(it.Get(), true);

  // Reset the UNDO manager
  // m_UndoManager.Clear();
}

LabelType
IRISApplication
::DrawOverLabel(LabelType iTarget)
{
  // Get the current merge settings
  CoverageModeType iMode = m_GlobalState->GetCoverageMode();
  LabelType iDrawing = m_GlobalState->GetDrawingColorLabel();
  LabelType iDrawOver = m_GlobalState->GetOverWriteColorLabel();  

  // Assign the output intensity based on the current drawing mode    
  bool visible = m_ColorLabelTable->GetColorLabel(iTarget).IsVisible();

  // If mode is paint over all, the victim is overridden
  return
     ((iMode == PAINT_OVER_ALL) ||
      (iMode == PAINT_OVER_COLORS && visible) ||
      (iMode == PAINT_OVER_ONE && iDrawOver == iTarget)) ? iDrawing : iTarget;
}

void 
IRISApplication
::UpdateIRISWithSnapImageData(CommandType *progressCommand)
{
  assert(m_SNAPImageData != NULL);

  // Get pointers to the source and destination images
  typedef LevelSetImageWrapper::ImageType SourceImageType;
  typedef LabelImageWrapper::ImageType TargetImageType;

  // If the voxel size of the image does not match the voxel size of the 
  // main image, we need to resample the region  
  SourceImageType::Pointer source = m_SNAPImageData->GetSnake()->GetImage();
  TargetImageType::Pointer target = m_IRISImageData->GetSegmentation()->GetImage();

  // Construct are region of interest into which the result will be pasted
  SNAPSegmentationROISettings roi = m_GlobalState->GetSegmentationROISettings();

  // If the ROI has been resampled, resample the segmentation in reverse direction
  if(roi.GetResampleFlag())
    {
    // Create a resampling filter
    typedef itk::ResampleImageFilter<SourceImageType,SourceImageType> ResampleFilterType;
    ResampleFilterType::Pointer fltSample = ResampleFilterType::New();

    // Initialize the resampling filter with an identity transform
    fltSample->SetInput(source);
    fltSample->SetTransform(itk::IdentityTransform<double,3>::New());

    // Typedefs for interpolators
    typedef itk::NearestNeighborInterpolateImageFunction<
      SourceImageType,double> NNInterpolatorType;
    typedef itk::LinearInterpolateImageFunction<
      SourceImageType,double> LinearInterpolatorType;
    typedef itk::BSplineInterpolateImageFunction<
      SourceImageType,double> CubicInterpolatorType;

    // More typedefs are needed for the sinc interpolator
    const unsigned int VRadius = 5;
    typedef itk::Function::HammingWindowFunction<VRadius> WindowFunction;
    typedef itk::ConstantBoundaryCondition<SourceImageType> Condition;
    typedef itk::WindowedSincInterpolateImageFunction<
      SourceImageType, VRadius, 
      WindowFunction, Condition, double> SincInterpolatorType;

    // Choose the interpolator
    switch(roi.GetInterpolationMethod())
      {
      case SNAPSegmentationROISettings::NEAREST_NEIGHBOR :
        fltSample->SetInterpolator(NNInterpolatorType::New());
        break;

      case SNAPSegmentationROISettings::TRILINEAR : 
        fltSample->SetInterpolator(LinearInterpolatorType::New());
        break;

      case SNAPSegmentationROISettings::TRICUBIC :
        fltSample->SetInterpolator(CubicInterpolatorType::New());
        break;  

      case SNAPSegmentationROISettings::SINC_WINDOW_05 :
        fltSample->SetInterpolator(SincInterpolatorType::New());
        break;
      };

    // Set the image sizes and spacing. We are creating an image of the 
    // dimensions of the ROI defined in the IRIS image space. 
    fltSample->SetSize(roi.GetROI().GetSize());
    fltSample->SetOutputSpacing(target->GetSpacing());
    fltSample->SetOutputOrigin(source->GetOrigin());

    // Watch the segmentation progress
    if(progressCommand) 
      fltSample->AddObserver(itk::AnyEvent(),progressCommand);

    // Set the unknown intensity to positive value
    fltSample->SetDefaultPixelValue(4.0f);

    // Perform resampling
    fltSample->UpdateLargestPossibleRegion();
    
    // Change the source to the output
    source = fltSample->GetOutput();
    }  
  
  // Create iterators for copying from one to the other
  typedef ImageRegionConstIterator<SourceImageType> SourceIteratorType;
  typedef ImageRegionIterator<TargetImageType> TargetIteratorType;
  SourceIteratorType itSource(source,source->GetLargestPossibleRegion());
  TargetIteratorType itTarget(target,roi.GetROI());

  // Figure out which color draws and which color is clear
  unsigned int iClear = m_GlobalState->GetPolygonInvert() ? 1 : 0;

  // Construct a merge table that contains an output intensity for every 
  // possible combination of two input intensities (note that snap image only
  // has two possible intensities
  LabelType mergeTable[2][MAX_COLOR_LABELS];

  // Perform the merge
  for(unsigned int i=0;i<MAX_COLOR_LABELS;i++)
    {
    // Whe the SNAP image is clear, IRIS passes through to the output
    // except for the IRIS voxels of the drawing color, which get cleared out
    mergeTable[iClear][i] = (i!=m_GlobalState->GetDrawingColorLabel()) ? i : 0;

    // If mode is paint over all, the victim is overridden
    mergeTable[1-iClear][i] = DrawOverLabel((LabelType) i);
    }

  // Go through both iterators, copy the new over the old
  itSource.GoToBegin();
  itTarget.GoToBegin();
  while(!itSource.IsAtEnd())
    {
    // Get the two voxels
    LabelType &voxIRIS = itTarget.Value();    
    float voxSNAP = itSource.Value();

    // Check that we're ok (debug mode only)
    assert(!itTarget.IsAtEnd());

    // Perform the merge
    voxIRIS = mergeTable[voxSNAP <= 0 ? 1 : 0][voxIRIS];

    // Iterate
    ++itSource;
    ++itTarget;
    }

  // The target has been modified
  target->Modified();
}

void
IRISApplication
::SetCursorPosition(const Vector3ui cursor)
{
  m_GlobalState->SetCrosshairsPosition(cursor); 
  this->GetCurrentImageData()->SetCrosshairs(cursor);
}

Vector3ui
IRISApplication
::GetCursorPosition() const
{
  return m_GlobalState->GetCrosshairsPosition();
}

void 
IRISApplication
::StoreUndoPoint(const char *text)
{
  // Set the current state as the undo point. We store the difference between
  // the last 'undo' image and the current segmentation image, and then copy
  // the current segmentation image into the undo image
  LabelImageWrapper *undo = m_IRISImageData->GetUndoImage();
  LabelImageWrapper *seg = m_IRISImageData->GetSegmentation();
  
  LabelType *dseg = seg->GetVoxelPointer();
  LabelType *dundo = undo->GetVoxelPointer();
  size_t n = seg->GetNumberOfVoxels();

  // Create the Undo delta object
  UndoManagerType::Delta *delta = new UndoManagerType::Delta();

  // Copy and encode
  for(size_t i = 0; i < n; i++)
    {
    LabelType vSrc = dseg[i], vDst = dundo[i];
    delta->Encode(vSrc - vDst);
    dundo[i] = vSrc;
    }

  // Important last step!
  delta->FinishEncoding();

  // Set modified flag on the undo image
  undo->GetImage()->Modified();

  // Add the delta object
  m_UndoManager.AppendDelta(delta);
}

bool
IRISApplication
::IsUndoPossible()
{
  return m_UndoManager.IsUndoPossible();
}

void
IRISApplication
::Undo()
{
  // In order to undo, we must take the 'current' delta and apply
  // it to the image
  UndoManagerType::Delta *delta = m_UndoManager.GetDeltaForUndo();

  LabelImageWrapper *imUndo = m_IRISImageData->GetUndoImage();
  LabelImageWrapper *imSeg = m_IRISImageData->GetSegmentation();
  LabelType *dundo = imUndo->GetVoxelPointer();
  LabelType *dseg = imSeg->GetVoxelPointer();

  // Applying the delta means adding 
  for(size_t i = 0; i < delta->GetNumberOfRLEs(); i++)
    {
    size_t n = delta->GetRLELength(i);
    LabelType d = delta->GetRLEValue(i);
    if(d == 0)
      {
      dundo += n;
      dseg += n;
      }
    else
      {
      for(size_t j = 0; j < n; j++)
        {
        *dundo -= d;
        *dseg = *dundo;
        ++dundo; ++dseg;
        }
      }
    }

  // Set modified flags
  imSeg->GetImage()->Modified();
  imUndo->GetImage()->Modified();
}


bool
IRISApplication
::IsRedoPossible()
{
  return m_UndoManager.IsRedoPossible();
}

void
IRISApplication
::Redo()
{
  // In order to undo, we must take the 'current' delta and apply
  // it to the image
  UndoManagerType::Delta *delta = m_UndoManager.GetDeltaForRedo();

  LabelImageWrapper *imUndo = m_IRISImageData->GetUndoImage();
  LabelImageWrapper *imSeg = m_IRISImageData->GetSegmentation();
  LabelType *dundo = imUndo->GetVoxelPointer();
  LabelType *dseg = imSeg->GetVoxelPointer();

  // Applying the delta means adding 
  for(size_t i = 0; i < delta->GetNumberOfRLEs(); i++)
    {
    size_t n = delta->GetRLELength(i);
    LabelType d = delta->GetRLEValue(i);
    if(d == 0)
      {
      dundo += n;
      dseg += n;
      }
    else
      {
      for(size_t j = 0; j < n; j++)
        {
        *dundo += d;
        *dseg = *dundo;
        ++dundo; ++dseg;
        }
      }
    }

  // Set modified flags
  imSeg->GetImage()->Modified();
  imUndo->GetImage()->Modified();
}




void 
IRISApplication
::ReleaseSNAPImageData() 
{
  assert(m_SNAPImageData && m_CurrentImageData != m_SNAPImageData);

  delete m_SNAPImageData;
  m_SNAPImageData = NULL;
}

void 
IRISApplication
::SetCurrentImageDataToIRIS() 
{
  assert(m_IRISImageData);
  m_CurrentImageData = m_IRISImageData;
}

void IRISApplication
::SetCurrentImageDataToSNAP() 
{
  assert(m_SNAPImageData);
  m_CurrentImageData = m_SNAPImageData;
}

size_t 
IRISApplication
::GetImageDirectionForAnatomicalDirection(AnatomicalDirection iAnat)
{
  string rai1 = "SRA", rai2 = "ILP";
  char c1 = rai1[iAnat], c2 = rai2[iAnat];
  for(size_t j = 0; j < 3; j++)
    {
    if(
      m_ImageToAnatomyRAI[j] == c1 || 
      m_ImageToAnatomyRAI[j] == c2)
      {
      return j;
      }
    }
  assert(0);
  return 0;
}

size_t 
IRISApplication
::GetDisplayWindowForAnatomicalDirection(
  AnatomicalDirection iAnat)
{
  string rai1 = "SRA", rai2 = "ILP";
  char c1 = rai1[iAnat], c2 = rai2[iAnat];
  for(size_t j = 0; j < 3; j++)
    {
    char sd = m_DisplayToAnatomyRAI[j][2];
    if(sd == c1 || sd == c2)
      return j;
    }

  assert(0);
  return 0;
}

void
IRISApplication
::ExportSlice(AnatomicalDirection iSliceAnat, const char *file)
{
  // Get the slice index in image coordinates
  size_t iSliceImg = 
    GetImageDirectionForAnatomicalDirection(iSliceAnat);
  
  // Get the grey slice
  GreyImageWrapper::DisplaySlicePointer imgGrey = 
    m_CurrentImageData->GetGrey()->GetDisplaySlice(iSliceImg);

  // Flip the image in the Y direction
  typedef itk::FlipImageFilter<GreyImageWrapper::DisplaySliceType> FlipFilter;
  FlipFilter::Pointer fltFlip = FlipFilter::New();
  fltFlip->SetInput(imgGrey);
  
  FlipFilter::FlipAxesArrayType arrFlips;
  arrFlips[0] = false; arrFlips[1] = true;
  fltFlip->SetFlipAxes(arrFlips);

  // Create a writer for saving the image
  typedef itk::ImageFileWriter<GreyImageWrapper::DisplaySliceType> WriterType;
  WriterType::Pointer writer = WriterType::New();
  writer->SetInput(fltFlip->GetOutput());
  writer->SetFileName(file);
  writer->Update();
}

void 
IRISApplication
::ExportSegmentationStatistics(const char *file)  throw(ExceptionObject)
{
  unsigned int i;
  
  // Make sure that the segmentation image exists
  assert(m_CurrentImageData->IsSegmentationLoaded());

  // A structure to describe each label
  struct Entry {
    unsigned long int count;
    unsigned long int sumGrey;
    unsigned long int sumGreySqr;
    double mean;
    double stddev;
  
    Entry() : count(0),sumGrey(0),sumGreySqr(0),mean(0),stddev(0) {}
  };
  
  // histogram of the segmentation image
  Entry data[MAX_COLOR_LABELS];

  // Create an iterator for parsing the segmentation image
  LabelImageWrapper::ConstIterator itLabel = 
    m_CurrentImageData->GetSegmentation()->GetImageConstIterator();

  // Another iterator for accessing the grey image
  GreyImageWrapper::ConstIterator itGrey = 
    m_CurrentImageData->GetGrey()->GetImageConstIterator();

  // Compute the number, sum and sum of squares of grey intensities for each 
  // label
  while(!itLabel.IsAtEnd())
    {
    LabelType label = itLabel.Value();
    GreyType grey = itGrey.Value();

    data[label].count++;
    data[label].sumGrey += grey;
    data[label].sumGreySqr += grey * grey;

    ++itLabel;
    ++itGrey;
    }
  
  // Compute the mean and standard deviation
  for (i=0; i<MAX_COLOR_LABELS; i++)
    {
    // The mean
    data[i].mean = data[i].sumGrey * 1.0 / data[i].count;

    // The standard deviation
    data[i].stddev = sqrt(
      (data[i].sumGreySqr * 1.0 - data[i].count * data[i].mean * data[i].mean) 
      / (data[i].count - 1));
    }
  
  // Compute the size of a voxel, in mm^3
  const double *spacing = 
    m_CurrentImageData->GetGrey()->GetImage()->GetSpacing().GetDataPointer();
  double volVoxel = spacing[0] * spacing[1] * spacing[2];

  // Open the selected file for writing
  std::ofstream fout(file);

  // Check if the file is readable
  if(!fout.good())
    throw itk::ExceptionObject(__FILE__, __LINE__,
                               "File can not be opened for writing");
  try 
    {
    // Write voxel volumes to the file
    fout << "##########################################################" << std::endl;
    fout << "# SNAP Voxel Count File" << std::endl;
    fout << "# File format:" << std::endl;
    fout << "# LABEL: ID / NUMBER / VOLUME / MEAN / SD" << std::endl;
    fout << "# Fields:" << std::endl;
    fout << "#    LABEL         Label description" << std::endl;
    fout << "#    ID            The numerical id of the label" << std::endl;
    fout << "#    NUMBER        Number of voxels that have that label " << std::endl;
    fout << "#    VOLUME        Volume of those voxels in cubic mm " << std::endl;
    fout << "#    MEAN          Mean intensity of those voxels " << std::endl;
    fout << "#    SD            Standard deviation of those voxels " << std::endl;
    fout << "##########################################################" << std::endl;

    for (i=1; i<MAX_COLOR_LABELS; i++) 
      {
      const ColorLabel &cl = m_ColorLabelTable->GetColorLabel(i);
      if(cl.IsValid() && data[i].count > 0)
        {
        fout << std::left << std::setw(40) << cl.GetLabel() << ": ";
        fout << std::right << std::setw(4) << i << " / ";
        fout << std::right << std::setw(10) << data[i].count << " / ";
        fout << std::setw(10) << (data[i].count * volVoxel) << " / ";
        fout << std::internal << std::setw(10) << data[i].mean << " / ";
        fout << std::setw(10) << data[i].stddev << std::endl;
        }      
      }
    }
  catch(...)
    {
    throw itk::ExceptionObject(__FILE__, __LINE__,
                           "File can not be written");
    }

  fout.close();
}



void
IRISApplication
::ExportSegmentationMesh(const MeshExportSettings &sets, itk::Command *progress) 
  throw(itk::ExceptionObject)
{
  // Based on the export settings, we will export one of the labels or all labels
  MeshObject mob;
  mob.Initialize(this);
  mob.GenerateVTKMeshes(progress);

  // If only one mesh is to be exported, life is easy
  if(sets.GetFlagSingleLabel())
    {
    for(size_t i = 0; i < mob.GetNumberOfVTKMeshes(); i++)
      {
      if(mob.GetVTKMeshLabel(i) == sets.GetExportLabel())
        {
        // Get the VTK mesh for the label
        vtkPolyData *mesh = mob.GetVTKMesh(i);

        // Export the mesh
        GuidedMeshIO io;
        Registry rFormat = sets.GetMeshFormat();
        io.SaveMesh(sets.GetMeshFileName().c_str(), rFormat, mesh);
        }
      }
    }

  mob.DiscardVTKMeshes();
}


void 
IRISApplication
::RelabelSegmentationWithCutPlane(const Vector3d &normal, double intercept) 
{
  // Get the label image
  LabelImageWrapper::ImagePointer imgLabel = 
    m_CurrentImageData->GetSegmentation()->GetImage();
  
  // Get an iterator for the image
  typedef ImageRegionIteratorWithIndex<
    LabelImageWrapper::ImageType> IteratorType;
  IteratorType it(imgLabel, imgLabel->GetBufferedRegion());

  // Compute a label mapping table based on the color labels
  LabelType table[MAX_COLOR_LABELS];
  
  // The clear label does not get painted over, no matter what
  table[0] = 0;

  // The other labels get painted over, depending on current settings
  for(unsigned int i = 1; i < MAX_COLOR_LABELS; i++)
    table[i] = DrawOverLabel(i);

  // Adjust the intercept by 0.5 for voxel offset
  intercept -= 0.5 * (normal[0] + normal[1] + normal[2]);

  // Iterate over the image, relabeling labels on one side of the plane
  while(!it.IsAtEnd())
    {
    // Compute the distance to the plane
    const long *index = it.GetIndex().GetIndex();
    double distance = 
      index[0]*normal[0] + 
      index[1]*normal[1] + 
      index[2]*normal[2] - intercept;

    // Check the side of the plane
    if(distance > 0)
      {
      LabelType &voxel = it.Value();
      voxel = table[voxel];
      }

    // Next voxel
    ++it;
    }
  
  // Register that the image has been updated
  imgLabel->Modified();
}

int 
IRISApplication
::GetRayIntersectionWithSegmentation(const Vector3d &point, 
                                     const Vector3d &ray, Vector3i &hit) const
{
  // Get the label wrapper
  LabelImageWrapper *xLabelWrapper = m_CurrentImageData->GetSegmentation();
  assert(xLabelWrapper->IsInitialized());

  Vector3ui lIndex;
  Vector3ui lSize = xLabelWrapper->GetSize();

  double delta[3][3], dratio[3];
  int    signrx, signry, signrz;

  double rx = ray[0];
  double ry = ray[1];
  double rz = ray[2];

  double rlen = rx*rx+ry*ry+rz*rz;
  if(rlen == 0) return -1;

  double rfac = 1.0 / sqrt(rlen);
  rx *= rfac; ry *= rfac; rz *= rfac;

  if (rx >=0) signrx = 1; else signrx = -1;
  if (ry >=0) signry = 1; else signry = -1;
  if (rz >=0) signrz = 1; else signrz = -1;

  // offset everything by (.5, .5) [becuz samples are at center of voxels]
  // this offset will put borders of voxels at integer values
  // we will work with this offset grid and offset back to check samples
  // we really only need to offset "point"
  double px = point[0]+0.5;
  double py = point[1]+0.5;
  double pz = point[2]+0.5;

  // get the starting point within data extents
  int c = 0;
  while ( (px < 0 || px >= lSize[0]||
           py < 0 || py >= lSize[1]||
           pz < 0 || pz >= lSize[2]) && c < 10000)
    {
    px += rx;
    py += ry;
    pz += rz;
    c++;
    }
  if (c >= 9999) return -1;

  // walk along ray to find intersection with any voxel with val > 0
  while ( (px >= 0 && px < lSize[0]&&
           py >= 0 && py < lSize[1] &&
           pz >= 0 && pz < lSize[2]) )
    {

    // offset point by (-.5, -.5) [to account for earlier offset] and
    // get the nearest sample voxel within unit cube around (px,py,pz)
    //    lx = my_round(px-0.5);
    //    ly = my_round(py-0.5);
    //    lz = my_round(pz-0.5);
    lIndex[0] = (int)px;
    lIndex[1] = (int)py;
    lIndex[2] = (int)pz;

    LabelType hitlabel = m_CurrentImageData->GetSegmentation()->GetVoxel(lIndex);
    const ColorLabel &cl = m_ColorLabelTable->GetColorLabel(hitlabel);

    if (cl.IsValid() && cl.IsVisible())
      {
      hit[0] = lIndex[0];
      hit[1] = lIndex[1];
      hit[2] = lIndex[2];
      return 1;
      }

    // BEGIN : walk along ray to border of next voxel touched by ray

    // compute path to YZ-plane surface of next voxel
    if (rx == 0)
      { // ray is parallel to 0 axis
      delta[0][0] = 9999;
      }
    else
      {
      delta[0][0] = (int)(px+signrx) - px;
      }

    // compute path to XZ-plane surface of next voxel
    if (ry == 0)
      { // ray is parallel to 1 axis
      delta[1][0] = 9999;
      }
    else
      {
      delta[1][1] = (int)(py+signry) - py;
      dratio[1]   = delta[1][1]/ry;
      delta[1][0] = dratio[1] * rx;
      }

    // compute path to XY-plane surface of next voxel
    if (rz == 0)
      { // ray is parallel to 2 axis
      delta[2][0] = 9999;
      }
    else
      {
      delta[2][2] = (int)(pz+signrz) - pz;
      dratio[2]   = delta[2][2]/rz;
      delta[2][0] = dratio[2] * rx;
      }

    // choose the shortest path 
    if ( fabs(delta[0][0]) <= fabs(delta[1][0]) && fabs(delta[0][0]) <= fabs(delta[2][0]) )
      {
      dratio[0]   = delta[0][0]/rx;
      delta[0][1] = dratio[0] * ry;
      delta[0][2] = dratio[0] * rz;
      px += delta[0][0];
      py += delta[0][1];
      pz += delta[0][2];
      }
    else if ( fabs(delta[1][0]) <= fabs(delta[0][0]) && fabs(delta[1][0]) <= fabs(delta[2][0]) )
      {
      delta[1][2] = dratio[1] * rz;
      px += delta[1][0];
      py += delta[1][1];
      pz += delta[1][2];
      }
    else
      { //if (fabs(delta[2][0] <= fabs(delta[0][0] && fabs(delta[2][0] <= fabs(delta[0][0]) 
      delta[2][1] = dratio[2] * ry;
      px += delta[2][0];
      py += delta[2][1];
      pz += delta[2][2];
      }
    // END : walk along ray to border of next voxel touched by ray

    } // while ( (px
  return 0;
}

void 
IRISApplication
::LoadGreyImageFile(const char *filename, const char *rai)
{
  // Load the settings associated with this file
  Registry regFull;
  m_SystemInterface->FindRegistryAssociatedWithFile(filename, regFull);
    
  // Get the folder dealing with grey image properties
  Registry &regGrey = regFull.Folder("Files.Grey");

  // To avoid loosing too much info, we'll load the image as 
  // a float and then rescale to the GreyType (short int).

  // Create the image reader
  GuidedImageIO<GreyLoadType> io;

  // Load the image (exception may occur here)
  GreyLoadImageType::Pointer imgGrey = io.ReadImage(filename, regGrey);

  // Get the orientation from the registry
  string sOrient = (rai == NULL)
    ? regGrey["Orientation"]["LPI"] // RFD: changed default from RAI to LPI
    : rai;

  // Set the image as the current grayscale image
  UpdateIRISGreyImage(imgGrey, sOrient.c_str());

  // Save the filename for the UI
  m_GlobalState->SetGreyFileName(filename);  
}


void 
IRISApplication
::LoadLabelImageFile(const char *filename)
{
  // Load the settings associated with this file
  Registry regFull;
  m_SystemInterface->FindRegistryAssociatedWithFile(filename, regFull);
    
  // Get the folder dealing with grey image properties
  // TODO: Figure out something about this!!!
  Registry &regGrey = regFull.Folder("Files.Grey");

  // Create the image reader
  GuidedImageIO<LabelType> io;
  
  // Load the image (exception may occur here)
  LabelImageType::Pointer imgLabel = io.ReadImage(filename, regGrey);

  // Set the image as the current grayscale image
  UpdateIRISSegmentationImage(imgLabel);

  // Save the filename for the UI
  m_GlobalState->SetSegmentationFileName(filename);  
}

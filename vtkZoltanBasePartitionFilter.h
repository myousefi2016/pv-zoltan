/*=========================================================================

  Module : vtkZoltanBasePartitionFilter

  Copyright (C) CSCS - Swiss National Supercomputing Centre.
  You may use modify and and distribute this code freely providing
  1) This copyright notice appears on all copies of source code
  2) An acknowledgment appears with any substantial usage of the code
  3) If this code is contributed to any other open source project, it
  must not be reformatted such that the indentation, bracketing or
  overall style is modified significantly.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

=========================================================================*/
//
// .NAME vtkZoltanBasePartitionFilter Efficiently distribute datasets in parallel
// .SECTION Description
// vtkZoltanBasePartitionFilter is the abstract base class used for parallel
// load balancing/partitioning using the Zoltan library from Trilinos.
//
// .SECTION See Also
// vtkParticlePartitionFilter, vtkMeshPartitionFilter
//
#ifndef __vtkZoltanBasePartitionFilter_h
#define __vtkZoltanBasePartitionFilter_h
//
#include <vector>                // std used throughout
#include <map>                   // std used throughout
#include <string>                // std used throughout
//
#include "vtkDataSetAlgorithm.h" // superclass
#include "vtkBoundingBox.h"      // used as parameter
#include "vtkSmartPointer.h"     // for memory safety
//
#include "zoltan.h"              // required for definitions

//BTX
#undef ZOLTAN_DEBUG_OUTPUT
//#define ZOLTAN_DEBUG_OUTPUT 1

#define DebugSynchronized(piece, numpieces, comm, msg) \
{ \
  for (int i=0; i<numpieces; ++i) { \
    if (piece==i) { \
      std::cout << "P(" << piece << "): " msg << "\n" << std::flush; \
    } \
    comm->Barrier(); \
  } \
}

#define DebugNonSynchronized(piece, numpieces, msg) \
{ \
  for (int i=0; i<numpieces; ++i) { \
    if (piece==i) { \
      std::cout << "P(" << piece << "): " msg << "\n" << std::flush; \
    } \
  } \
}

#ifdef ZOLTAN_DEBUG_OUTPUT
# define debug_no_sync(msg) \
  DebugNonSynchronized(this->UpdatePiece, this->UpdateNumPieces, msg);

# define debug_2(msg) \
  DebugSynchronized(callbackdata->self->UpdatePiece, \
    callbackdata->self->UpdateNumPieces, callbackdata->self->GetController(), msg);
#else
# define debug_no_sync(msg)
# define debug_2(msg)
#endif
//ETX
//
#define error_2(a) std::cout << a << " >>> FATAL ERROR : " \
  << callbackdata->ProcessRank << std::endl

// standard vtk classes
class  vtkMultiProcessController;
class  vtkPoints;
class  vtkIdTypeArray;
class  vtkIntArray;
class  vtkPointSet;
class  vtkDataSetAttributes;
class  vtkCellArray;
class  vtkTimerLog;
class  vtkFieldData;
class  vtkPKdTree;
// our special extent translator
class  vtkBoundsExtentTranslator;
class vtkInformationDataObjectMetaDataKey;
class vtkInformationIntegerRequestKey;
class vtkInformationIntegerKey;

//----------------------------------------------------------------------------
#ifdef ZOLTAN_DEBUG_OUTPUT
  #define INC_PACK_COUNT pack_count++;
  #define INC_UNPACK_COUNT unpack_count++;
  #define INC_SIZE_COUNT size_count++;
  #define CLEAR_ZOLTAN_DEBUG pack_count = 0; size_count = 0; unpack_count = 0;
#else
  #define INC_PACK_COUNT
  #define INC_UNPACK_COUNT
  #define INC_SIZE_COUNT
  #define CLEAR_ZOLTAN_DEBUG
#endif
//----------------------------------------------------------------------------
//
// GCC has trouble resolving some templated function pointers,
// we explicitly declare the types and then cast them as args where needed
//
typedef int  (*zsize_fn) (void *, int , int , ZOLTAN_ID_PTR , ZOLTAN_ID_PTR , int *);
typedef void (*zpack_fn) (void *, int , int , ZOLTAN_ID_PTR , ZOLTAN_ID_PTR , int , int , char *, int *);
typedef void (*zupack_fn)(void *, int , ZOLTAN_ID_PTR , int , char *, int *);
typedef void (*zprem_fn) (void *, int , int , int , ZOLTAN_ID_PTR , ZOLTAN_ID_PTR , int *, int *, int , ZOLTAN_ID_PTR , ZOLTAN_ID_PTR , int *, int *, int *);

// Zoltan 2 typedefs
typedef int localId_t;
#ifdef HAVE_ZOLTAN2_LONG_LONG_INT
typedef long long globalId_t;
#else
typedef int globalId_t;
#endif
//----------------------------------------------------------------------------
class vtkInformationDoubleKey;
class vtkInformationDoubleVectorKey;
class vtkInformationIntegerKey;
//----------------------------------------------------------------------------
class VTK_EXPORT vtkZoltanBasePartitionFilter : public vtkDataSetAlgorithm
{
  public:
    vtkTypeMacro(vtkZoltanBasePartitionFilter,vtkDataSetAlgorithm);
    void PrintSelf(ostream& os, vtkIndent indent);

    static vtkInformationDoubleVectorKey* ZOLTAN_SAMPLE_RESOLUTION();
    static vtkInformationDoubleVectorKey* ZOLTAN_SAMPLE_ORIGIN();

    // Description:
    // By default this filter uses the global controller,
    // but this method can be used to set another instead.
    virtual void SetController(vtkMultiProcessController*);
    vtkMultiProcessController *GetController() { return this->Controller; }

    // Description:
    // Set the maximum aspect ratio allowed for any pair of axes when subdividing
    vtkSetMacro(MaxAspectRatio, double);
    vtkGetMacro(MaxAspectRatio, double);

    // Description:
    // When reading time dependent data (for example), it may be necessary
    // to partition once, but send scalar fields at each time step.
    // In this case we need to keep the (Invert Lists) Partition info so that migration
    // steps may be performed after the initial partition
    vtkSetMacro(KeepInversePointLists, int);
    vtkGetMacro(KeepInversePointLists, int);
    vtkBooleanMacro(KeepInversePointLists, int);

    // Description:
    // If the input can be free during operation to make space for repartitioned data
    // use with extreme care. Modifying the input is not normal practice in VTK
    vtkSetMacro(InputDisposable, int);
    vtkGetMacro(InputDisposable, int);
    vtkBooleanMacro(InputDisposable, int);

    // Description:
    // Specify the name of the array to be used for point weights
    vtkSetStringMacro(PointWeightsArrayName);
    vtkGetStringMacro(PointWeightsArrayName);

    // Description:
    // Return the Bounding Box for a partition
    // only valid after the filter has executed
    vtkBoundingBox *GetPartitionBoundingBox(int partition);
    vtkBoundingBox *GetPartitionBoundingBoxHalo(int partition);

    // Description:
    // Return the Bounding Box of all the data (uses MPI to collect all boxes)
    vtkBoundingBox GetGlobalBounds(vtkDataSet *input);

    // Description:
    // Return the KdTree representing the decomposition of space
    // only valid after the filter has executed
    vtkSmartPointer<vtkPKdTree> GetKdtree() { return this->KdTree; }

    // Description:
    // Return the Imbalance value returned from Zoltan
    // only valid after the filter has executed
    float GetImbalanceValue() { return this->ImbalanceValue; }

    // Description:
    // The thickness of the region between each partition that is used for
    // ghost cell exchanges. Any cells within this overlap region of another
    // processor will be duplicated on neighbouring processors (possibly multiple times
    // at corner region overlaps)
    vtkSetMacro(GhostHaloSize, double);
    vtkGetMacro(GhostHaloSize, double);


    //----------------------------------------------------------------------------
    // Structure to hold all the dataset/mesh/points related data we pass to
    // and from zoltan during points/cell migration callbacks
    //----------------------------------------------------------------------------
    typedef struct CallbackData {
      vtkZoltanBasePartitionFilter         *self;
      int                                   ProcessRank;
      vtkSmartPointer<vtkPointSet>          Input;
      vtkSmartPointer<vtkPointSet>          Output;
      vtkSmartPointer<vtkDataSetAttributes> InputPointData;
      vtkSmartPointer<vtkDataSetAttributes> InputCellData;
      std::vector<int>              ProcessOffsetsPointId; // offsets into Ids for each process {0, N1, N1+N2, N1+N2+N3...}
      std::vector<int>              ProcessOffsetsCellId;  // offsets into Ids for each process {0, N1, N1+N2, N1+N2+N3...}
      int                           PointType;             // float/double flag
      void                         *InputPointsData;       // float/double pointer
      void                         *OutputPointsData;      // float/double pointer
      int                           MaxCellSize;
      vtkIdType                     OutPointCount;
      vtkIdType                     MigrationPointCount;
      vtkIdType                     OutCellCount;
      vtkSmartPointer<vtkCellArray> OutputUnstructuredCellArray;
      int                          *OutputUnstructuredCellTypes;
      std::vector<vtkIdType>        LocalToLocalIdMap;
      std::vector<vtkIdType>        LocalToLocalCellMap;
      std::map<vtkIdType,vtkIdType> ReceivedGlobalToLocalIdMap;
      //
      // The variables below are used twice, once for points, then again for cells
      // but the information is updated in between
      //
      int                           NumberOfFields;
      std::vector<void*>            InputArrayPointers;
      std::vector<void*>            OutputArrayPointers;
      std::vector<int>              MemoryPerTuple;
      int                           TotalSizePerId;
      std::vector<vtkIdType>        LocalIdsToKeep;
    } CallbackData;

    //----------------------------------------------------------------------------
    // Structure holding pointers that zoltan returns for lists of assignments
    // to processors etc
    //----------------------------------------------------------------------------
    typedef struct ZoltanLoadBalanceData {
      int changes, numGidEntries, numLidEntries, numImport, numExport;
      ZOLTAN_ID_PTR importGlobalGids;
      ZOLTAN_ID_PTR importLocalGids;
      ZOLTAN_ID_PTR exportGlobalGids;
      ZOLTAN_ID_PTR exportLocalGids;
      int *importProcs;
      int *importToPart;
      int *exportProcs;
      int *exportToPart;
    } ZoltanLoadBalanceData;

    //----------------------------------------------------------------------------
    // Structure we use as a temp storage for info about sending points to remote processors.
    // GlobalIds is the list of local Ids we are sending away, converted to Global Ids.
    // Procs is the list of destination ranks, must be same size as GlobalIds
    // LocalIdsToKeep is a (usually) small subset of Ids which need to be copied
    // locally as well as sent remotely.
    //----------------------------------------------------------------------------
    typedef struct PartitionInfo {
      // use either the vector
      std::vector<ZOLTAN_ID_TYPE> GlobalIds;
      std::vector<int>            Procs;
      // or the Ptr interface, if the Ptr vars are set, they will be used in preference to the vectors
      vtkIdType                   nIDs;
      ZOLTAN_ID_TYPE             *GlobalIdsPtr;
      int                        *ProcsPtr;
      // Points which are to be copied locally
      std::vector<vtkIdType>      LocalIdsToKeep;
      //
      PartitionInfo() : nIDs(0), GlobalIdsPtr(0), ProcsPtr(0) {}
    } PartitionInfo;

    typedef struct MigrationLists {
      PartitionInfo               known;
      int                         num_found;
      int                         num_reserved;
      ZOLTAN_ID_PTR               found_global_ids;
      ZOLTAN_ID_PTR               found_local_ids;
      int                        *found_procs;
      int                        *found_to_part;
      MigrationLists() : num_found(0),num_reserved(0), found_global_ids(0), found_local_ids(0), found_procs(0), found_to_part(0) {};
    } MigrationLists;

    // Description:
    // zoltan callback to return number of points participating in load/balance
    static int get_number_of_objects_points(void *data, int *ierr);

    // Description:
    // Zoltan callback which fills the Ids/weights for each point in the exchange
    static void get_object_list_points(void *data, int sizeGID, int sizeLID,
      ZOLTAN_ID_PTR globalID, ZOLTAN_ID_PTR localID,
      int wgt_dim, float *obj_wgts, int *ierr);

    // Description:
    // Zoltan callback which returns the dimension of geometry (3D for us)
    static int get_num_geometry(void *data, int *ierr);

    // Description:
    // Zoltan callback which returns coordinate geometry data (points)
    // templated here to alow float/double instances in our implementation
    template<typename T>
    static void get_geometry_list(
      void *data, int sizeGID, int sizeLID, int num_obj,
      ZOLTAN_ID_PTR globalID, ZOLTAN_ID_PTR localID,
      int num_dim, double *geom_vec, int *ierr);

    // Description:
    // A ZOLTAN_OBJ_SIZE_FN query function returns the size (in bytes) of the data buffer
    // that is needed to pack all of a single object's data.
    // Here we add up the size of all the field arrays for points + the geometry itself
    template<typename T>
    static int zoltan_obj_size_function_pointdata(void *data,
      int num_gid_entries, int num_lid_entries, ZOLTAN_ID_PTR global_id,
      ZOLTAN_ID_PTR local_id, int *ierr);

    // Description:
    // Zoltan callback to pack all the data for one point into a buffer
    template<typename T>
    static void zoltan_pack_obj_function_pointdata(void *data, int num_gid_entries, int num_lid_entries,
      ZOLTAN_ID_PTR global_id, ZOLTAN_ID_PTR local_id, int dest, int size, char *buf, int *ierr);

    // Description:
    // Zoltan callback to unpack all the data for one point from a buffer
    template<typename T>
    static void zoltan_unpack_obj_function_pointdata(void *data, int num_gid_entries,
      ZOLTAN_ID_PTR global_id, int size, char *buf, int *ierr);

    // Description:
    // Zoltan callback for Pre migration setup/initialization

    static void zoltan_pre_migrate_function_null(
      void *data, int num_gid_entries, int num_lid_entries,
      int num_import, ZOLTAN_ID_PTR import_global_ids, ZOLTAN_ID_PTR import_local_ids,
      int *import_procs, int *import_to_part, int num_export, ZOLTAN_ID_PTR export_global_ids,
      ZOLTAN_ID_PTR export_local_ids, int *export_procs, int *export_to_part, int *ierr);

    template<typename T>
    static void zoltan_pre_migrate_function_points(
      void *data, int num_gid_entries, int num_lid_entries,
      int num_import, ZOLTAN_ID_PTR import_global_ids, ZOLTAN_ID_PTR import_local_ids,
      int *import_procs, int *import_to_part, int num_export, ZOLTAN_ID_PTR export_global_ids,
      ZOLTAN_ID_PTR export_local_ids, int *export_procs, int *export_to_part, int *ierr);

    void InitializeFieldDataArrayPointers(
      CallbackData *callbackdata,
      vtkFieldData *infielddata,
      vtkFieldData *outfielddata,
      vtkIdType Nfinal);

    virtual void InitializeZoltanLoadBalance();
    static void add_Id_to_interval_map(CallbackData *data, vtkIdType GID, vtkIdType LID);
    vtkIdType   global_to_local_Id(vtkIdType GID);

    template<typename T>
    void CopyPointsToSelf(
      std::vector<vtkIdType> &LocalPointsToKeep, vtkIdType num_reserved,
      void *data, int num_gid_entries, int num_lid_entries,
      int num_import, ZOLTAN_ID_PTR import_global_ids, ZOLTAN_ID_PTR import_local_ids,
      int *import_procs, int *import_to_part, int num_export, ZOLTAN_ID_PTR export_global_ids,
      ZOLTAN_ID_PTR export_local_ids, int *export_procs, int *export_to_part, int *ierr);

    //
    // for migration of point data without geometry etc
    //
    static int zoltan_obj_size_function_pointdata(void *data,
      int num_gid_entries, int num_lid_entries, ZOLTAN_ID_PTR global_id,
      ZOLTAN_ID_PTR local_id, int *ierr);
    static void zoltan_pack_obj_function_pointdata(void *data, int num_gid_entries, int num_lid_entries,
      ZOLTAN_ID_PTR global_id, ZOLTAN_ID_PTR local_id, int dest, int size, char *buf, int *ierr);
    static void zoltan_unpack_obj_function_pointdata(void *data, int num_gid_entries,
      ZOLTAN_ID_PTR global_id, int size, char *buf, int *ierr);
    static void zoltan_pre_migrate_function_pointdata(
      void *data, int num_gid_entries, int num_lid_entries,
      int num_import, ZOLTAN_ID_PTR import_global_ids, ZOLTAN_ID_PTR import_local_ids,
      int *import_procs, int *import_to_part, int num_export, ZOLTAN_ID_PTR export_global_ids,
      ZOLTAN_ID_PTR export_local_ids, int *export_procs, int *export_to_part, int *ierr);

  // Description:
  // After an initial partitioning has taken place, this function can be used to partition
  // additional filed arrays such as scalar data at t>0.
  // to make use of this function, the KeepInversePointLists must be enabled when
  // initial partitioning takes place.
  bool MigratePointData(vtkDataSetAttributes *inPointData, vtkDataSetAttributes *outPointData);

  void AllocateFieldArrays(vtkDataSetAttributes *fields, vtkDataSetAttributes *fieldcopy);

  // utility function to find average of point list
  template <typename T>
  void FindCentroid(int npts, vtkIdType *pts, CallbackData *callbackdata, T avg[3])
  {
      avg[0] = avg[1] = avg[2] = 0.0;
      for (int i=0; i<npts; ++i) {
          vtkIdType ptId = pts[i];
          avg[0] += ((T*)(callbackdata->InputPointsData))[3*ptId+0];
          avg[1] += ((T*)(callbackdata->InputPointsData))[3*ptId+1];
          avg[2] += ((T*)(callbackdata->InputPointsData))[3*ptId+2];
      }
      avg[0] /= npts;
      avg[1] /= npts;
      avg[2] /= npts;
  }

  template <typename T>
  int FindProcessFromPoint(T *pt)
  {
      for (int proc=0; proc<this->UpdateNumPieces; proc++) {
        vtkBoundingBox &b = this->BoxList[proc];
        if (b.ContainsPoint(pt[0], pt[1], pt[2])) {
            return proc;
        }
      }
      return -1;
  }

    int                                         UpdatePiece;
    int                                         UpdateNumPieces;

    // flag for verts, lines, polys, strips to ensure ranks with
    // no data can correctly initialize output
    int                                         polydata_types;

  protected:
     vtkZoltanBasePartitionFilter();
    ~vtkZoltanBasePartitionFilter();

    virtual void ComputeIdOffsets(vtkIdType Npoints, vtkIdType Ncells);

    int  GatherDataTypeInfo(vtkDataSet *input, vtkPoints *points);
    bool GatherDataArrayInfo(vtkDataArray *data,  vtkDataSetAttributes *attribs,
        int &datatype, std::string &dataname, int &numComponents, int &attrib);

    // Override to specify support for vtkPointSet input type.
    virtual int FillInputPortInformation(int port, vtkInformation* info);

    // Override to specify different type of output
    virtual int FillOutputPortInformation(int port, vtkInformation* info);

    // Description:
    virtual int RequestInformation(vtkInformation*,
                                   vtkInformationVector**,
                                   vtkInformationVector*);
    // Description:
    virtual int RequestUpdateExtent(vtkInformation*,
                                    vtkInformationVector**,
                                    vtkInformationVector*);

    // Description:
    virtual int RequestData(vtkInformation*,
                            vtkInformationVector**,
                            vtkInformationVector*);

    MPI_Comm GetMPIComm();

    virtual void ExecuteZoltanPartition(vtkPointSet *output, vtkPointSet *input) = 0;
    virtual void GetZoltanBoundingBoxes(vtkBoundingBox &globalBounds) = 0;

    int PartitionPoints(vtkInformation* info, vtkInformationVector** inputVector, vtkInformationVector* outputVector);

    void ComputeInvertLists(MigrationLists &migrationLists);
    int ManualPointMigrate(MigrationLists &migrationLists, bool keepinformation);
    int ZoltanPointMigrate(MigrationLists &migrationLists, bool keepinformation);

    vtkSmartPointer<vtkPKdTree> CreatePkdTree();

    void AddHaloToBoundingBoxes(double GhostCellOverlap);
    void SetupPointWeights(vtkDataSetAttributes *fields);

    //
    vtkBoundingBox                             *LocalBox;
    std::vector<vtkBoundingBox>                 BoxList;
    std::vector<vtkBoundingBox>                 BoxListWithHalo;
    double                                      GhostHaloSize;
    //
    vtkMultiProcessController                  *Controller;
     //
    double                                      MaxAspectRatio;
    int                                         KeepInversePointLists;
    int                                         InputDisposable;
    vtkSmartPointer<vtkBoundsExtentTranslator>  ExtentTranslator;
    vtkSmartPointer<vtkBoundsExtentTranslator>  InputExtentTranslator;
    vtkSmartPointer<vtkPKdTree>                 KdTree;
    vtkSmartPointer<vtkTimerLog>                Timer;
    MigrationLists                              MigrateLists;
    //
    char                                       *PointWeightsArrayName;
    void                                       *weights_data_ptr;
    //
    struct Zoltan_Struct       *ZoltanData;
    CallbackData                ZoltanCallbackData;
    ZoltanLoadBalanceData       LoadBalanceData;
    //
    float                       ImbalanceValue;

#ifdef ZOLTAN_DEBUG_OUTPUT
    //
    // For debugging
    //
    static int pack_count;
    static int unpack_count;
    static int size_count;
#endif

  private:
    vtkZoltanBasePartitionFilter(const vtkZoltanBasePartitionFilter&);  // Not implemented.
    void operator=(const vtkZoltanBasePartitionFilter&);  // Not implemented.
};

#endif

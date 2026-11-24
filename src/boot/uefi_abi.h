/* Freestanding UEFI 2.10 C ABI definitions for FinnOS Graphical Bootloader.
 * Covers GOP, Simple Text In/Out, Simple File System, Loaded Image, Device Path,
 * Simple/Absolute Pointer, LoadFile2 (Linux EFI stub initrd), and systemd
 * Boot Loader Interface EFI variables (4a67b082-0a4c-41cf-b6c7-440b29bb8c4f).
 */

#ifndef FINN_BOOT_UEFI_ABI_H
#define FINN_BOOT_UEFI_ABI_H

#include <stdint.h>
#include <stddef.h>

#if defined(__x86_64__) || defined(_M_X64)
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

typedef uint64_t UINTN;
typedef int64_t  INTN;
typedef uint64_t UINT64;
typedef uint32_t UINT32;
typedef int32_t  INT32;
typedef uint16_t UINT16;
typedef uint8_t  UINT8;
typedef uint8_t  BOOLEAN;
typedef uint16_t CHAR16;
typedef void*    EFI_HANDLE;
typedef void*    EFI_EVENT;
typedef UINTN    EFI_STATUS;
typedef UINT64   EFI_PHYSICAL_ADDRESS;
typedef UINT64   EFI_VIRTUAL_ADDRESS;

#define EFI_SUCCESS              0ULL
#define EFI_ERROR_BIT            (1ULL << 63)
#define EFI_LOAD_ERROR           (EFI_ERROR_BIT | 1ULL)
#define EFI_INVALID_PARAMETER    (EFI_ERROR_BIT | 2ULL)
#define EFI_UNSUPPORTED          (EFI_ERROR_BIT | 3ULL)
#define EFI_BUFFER_TOO_SMALL     (EFI_ERROR_BIT | 5ULL)
#define EFI_NOT_READY            (EFI_ERROR_BIT | 6ULL)
#define EFI_NOT_FOUND            (EFI_ERROR_BIT | 14ULL)
#define EFI_ERROR(s)             (((INTN)(s)) < 0)

#define EFI_VARIABLE_NON_VOLATILE       0x00000001U
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 0x00000002U
#define EFI_VARIABLE_RUNTIME_ACCESS     0x00000004U

#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL 0x00000001U

#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE  0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL
#define EFI_FILE_DIRECTORY   0x0000000000000010ULL

#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_LOADER_DATA        2

#define EFI_RESET_COLD 0
#define EFI_RESET_WARM 1
#define EFI_RESET_SHUTDOWN 2

#define EFI_OS_INDICATIONS_BOOT_TO_FW_UI 0x0000000000000001ULL

/* systemd Boot Loader Interface feature flags (LoaderFeatures) */
#define EFI_LOADER_FEATURE_CONFIG_TIMEOUT          (1ULL << 0)
#define EFI_LOADER_FEATURE_CONFIG_TIMEOUT_ONE_SHOT (1ULL << 1)
#define EFI_LOADER_FEATURE_ENTRY_DEFAULT           (1ULL << 2)
#define EFI_LOADER_FEATURE_ENTRY_ONESHOT           (1ULL << 3)
#define EFI_LOADER_FEATURE_BOOT_COUNTING           (1ULL << 4)
#define EFI_LOADER_FEATURE_XBOOTLDR                (1ULL << 5)
#define EFI_LOADER_FEATURE_SORT_KEY                (1ULL << 8)
#define EFI_LOADER_FEATURE_SAVED_ENTRY             (1ULL << 9)

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    UINT16 Year;
    UINT8  Month;
    UINT8  Day;
    UINT8  Hour;
    UINT8  Minute;
    UINT8  Second;
    UINT8  Pad1;
    UINT32 Nanosecond;
    int16_t TimeZone;
    UINT8  Daylight;
    UINT8  Pad2;
} EFI_TIME;

typedef struct {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *ReadKeyStroke)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key);
    EFI_EVENT WaitForKey;
};

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, const CHAR16 *String);
    EFI_STATUS (EFIAPI *TestString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, const CHAR16 *String);
    EFI_STATUS (EFIAPI *QueryMode)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber, UINTN *Columns, UINTN *Rows);
    EFI_STATUS (EFIAPI *SetMode)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber);
    EFI_STATUS (EFIAPI *SetAttribute)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute);
    EFI_STATUS (EFIAPI *ClearScreen)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);
    EFI_STATUS (EFIAPI *SetCursorPosition)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Column, UINTN Row);
    EFI_STATUS (EFIAPI *EnableCursor)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN Visible);
    void *Mode;
};

/* Graphics Output Protocol (GOP) */
typedef struct {
    UINT8 Blue;
    UINT8 Green;
    UINT8 Red;
    UINT8 Reserved;
} EFI_GRAPHICS_OUTPUT_BLT_PIXEL;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    UINT32 PixelFormat;
    UINT32 PixelInformation[4];
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN  SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN  FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *QueryMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber, UINTN *SizeOfInfo, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);
    EFI_STATUS (EFIAPI *SetMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber);
    EFI_STATUS (EFIAPI *Blt)(EFI_GRAPHICS_OUTPUT_PROTOCOL *This, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer,
                             UINT32 BltOperation, UINTN SourceX, UINTN SourceY,
                             UINTN DestinationX, UINTN DestinationY,
                             UINTN Width, UINTN Height, UINTN Delta);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

#define EfiBltVideoFill        0
#define EfiBltBufferToVideo    2

/* Simple Pointer Protocol */
typedef struct {
    INT32   RelativeMovementX;
    INT32   RelativeMovementY;
    INT32   RelativeMovementZ;
    BOOLEAN LeftButton;
    BOOLEAN RightButton;
} EFI_SIMPLE_POINTER_STATE;

typedef struct _EFI_SIMPLE_POINTER_PROTOCOL EFI_SIMPLE_POINTER_PROTOCOL;
struct _EFI_SIMPLE_POINTER_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_POINTER_PROTOCOL *This, BOOLEAN ExtendedVerification);
    EFI_STATUS (EFIAPI *GetState)(EFI_SIMPLE_POINTER_PROTOCOL *This, EFI_SIMPLE_POINTER_STATE *State);
    EFI_EVENT WaitForInput;
    void *Mode;
};

/* Device Path */
#pragma pack(push, 1)
typedef struct {
    UINT8  Type;
    UINT8  SubType;
    UINT16 Length;
} EFI_DEVICE_PATH_PROTOCOL;

typedef struct {
    EFI_DEVICE_PATH_PROTOCOL Header;
    UINT32 PartitionNumber;
    UINT64 PartitionStart;
    UINT64 PartitionSize;
    UINT8  Signature[16];
    UINT8  MBRType;
    UINT8  SignatureType;
} HARDDRIVE_DEVICE_PATH;

typedef struct {
    EFI_DEVICE_PATH_PROTOCOL Header;
    EFI_GUID VendorGuid;
} VENDOR_DEVICE_PATH;
#pragma pack(pop)

/* File Protocol */
typedef struct {
    UINT64   Size;
    UINT64   FileSize;
    UINT64   PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64   Attribute;
    CHAR16   FileName[1];
} EFI_FILE_INFO;

typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct _EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *Open)(EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **NewHandle, const CHAR16 *FileName, UINT64 OpenMode, UINT64 Attributes);
    EFI_STATUS (EFIAPI *Close)(EFI_FILE_PROTOCOL *This);
    EFI_STATUS (EFIAPI *Delete)(EFI_FILE_PROTOCOL *This);
    EFI_STATUS (EFIAPI *Read)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Write)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, const void *Buffer);
    EFI_STATUS (EFIAPI *GetPosition)(EFI_FILE_PROTOCOL *This, UINT64 *Position);
    EFI_STATUS (EFIAPI *SetPosition)(EFI_FILE_PROTOCOL *This, UINT64 Position);
    EFI_STATUS (EFIAPI *GetInfo)(EFI_FILE_PROTOCOL *This, const EFI_GUID *InformationType, UINTN *BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *SetInfo)(EFI_FILE_PROTOCOL *This, const EFI_GUID *InformationType, UINTN BufferSize, void *Buffer);
    EFI_STATUS (EFIAPI *Flush)(EFI_FILE_PROTOCOL *This);
};

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);
};

/* Loaded Image Protocol */
typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    void *SystemTable;
    EFI_HANDLE DeviceHandle;
    EFI_DEVICE_PATH_PROTOCOL *FilePath;
    void *Reserved;
    UINT32 LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    UINT64 ImageSize;
    UINT32 ImageCodeType;
    UINT32 ImageDataType;
    EFI_STATUS (EFIAPI *Unload)(EFI_HANDLE ImageHandle);
} EFI_LOADED_IMAGE_PROTOCOL;

/* LoadFile2 Protocol (Linux EFI stub initrd delivery) */
typedef struct _EFI_LOAD_FILE2_PROTOCOL EFI_LOAD_FILE2_PROTOCOL;
struct _EFI_LOAD_FILE2_PROTOCOL {
    EFI_STATUS (EFIAPI *LoadFile)(EFI_LOAD_FILE2_PROTOCOL *This,
                                  EFI_DEVICE_PATH_PROTOCOL *FilePath,
                                  BOOLEAN BootPolicy,
                                  UINTN *BufferSize,
                                  void *Buffer);
};

/* Runtime Services (exact UEFI 2.10 table layout) */
typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *GetTime;
    void *SetTime;
    void *GetWakeupTime;
    void *SetWakeupTime;
    void *SetVirtualAddressMap;
    void *ConvertPointer;
    EFI_STATUS (EFIAPI *GetVariable)(const CHAR16 *VariableName, const EFI_GUID *VendorGuid, UINT32 *Attributes, UINTN *DataSize, void *Data);
    void *GetNextVariableName;
    EFI_STATUS (EFIAPI *SetVariable)(const CHAR16 *VariableName, const EFI_GUID *VendorGuid, UINT32 Attributes, UINTN DataSize, const void *Data);
    void *GetNextHighMonotonicCount;
    void (EFIAPI *ResetSystem)(UINT32 ResetType, EFI_STATUS ResetStatus, UINTN DataSize, void *ResetData);
} EFI_RUNTIME_SERVICES;

/* Boot Services (exact UEFI 2.10 table layout) */
typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_STATUS (EFIAPI *AllocatePages)(UINT32 Type, UINT32 MemoryType, UINTN Pages, EFI_PHYSICAL_ADDRESS *Memory);
    EFI_STATUS (EFIAPI *FreePages)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
    void *GetMemoryMap;
    EFI_STATUS (EFIAPI *AllocatePool)(UINT32 PoolType, UINTN Size, void **Buffer);
    EFI_STATUS (EFIAPI *FreePool)(void *Buffer);
    void *CreateEvent;
    void *SetTimer;
    EFI_STATUS (EFIAPI *WaitForEvent)(UINTN NumberOfEvents, EFI_EVENT *Event, UINTN *Index);
    void *SignalEvent;
    void *CloseEvent;
    EFI_STATUS (EFIAPI *CheckEvent)(EFI_EVENT Event);
    EFI_STATUS (EFIAPI *InstallProtocolInterface)(EFI_HANDLE *Handle, const EFI_GUID *Protocol, UINT32 InterfaceType, void *Interface);
    void *ReinstallProtocolInterface;
    EFI_STATUS (EFIAPI *UninstallProtocolInterface)(EFI_HANDLE Handle, const EFI_GUID *Protocol, void *Interface);
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE Handle, const EFI_GUID *Protocol, void **Interface);
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    EFI_STATUS (EFIAPI *LoadImage)(BOOLEAN BootPolicy, EFI_HANDLE ParentImageHandle, EFI_DEVICE_PATH_PROTOCOL *DevicePath, void *SourceBuffer, UINTN SourceSize, EFI_HANDLE *ImageHandle);
    EFI_STATUS (EFIAPI *StartImage)(EFI_HANDLE ImageHandle, UINTN *ExitDataSize, CHAR16 **ExitData);
    EFI_STATUS (EFIAPI *Exit)(EFI_HANDLE ImageHandle, EFI_STATUS ExitStatus, UINTN ExitDataSize, CHAR16 *ExitData);
    EFI_STATUS (EFIAPI *UnloadImage)(EFI_HANDLE ImageHandle);
    void *ExitBootServices;
    void *GetNextMonotonicCount;
    EFI_STATUS (EFIAPI *Stall)(UINTN Microseconds);
    EFI_STATUS (EFIAPI *SetWatchdogTimer)(UINTN Timeout, UINT64 WatchdogCode, UINTN DataSize, CHAR16 *WatchdogData);
    void *ConnectController;
    void *DisconnectController;
    EFI_STATUS (EFIAPI *OpenProtocol)(EFI_HANDLE Handle, const EFI_GUID *Protocol, void **Interface, EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle, UINT32 Attributes);
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    EFI_STATUS (EFIAPI *LocateHandleBuffer)(UINT32 SearchType, const EFI_GUID *Protocol, void *SearchKey, UINTN *NoHandles, EFI_HANDLE **Buffer);
    EFI_STATUS (EFIAPI *LocateProtocol)(const EFI_GUID *Protocol, void *Registration, void **Interface);
    EFI_STATUS (EFIAPI *InstallMultipleProtocolInterfaces)(EFI_HANDLE *Handle, ...);
    EFI_STATUS (EFIAPI *UninstallMultipleProtocolInterfaces)(EFI_HANDLE Handle, ...);
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* Standard UEFI & systemd-boot GUIDs */
static const EFI_GUID gEfiGlobalVariableGuid =
    {0x8BE4DF61, 0x93CA, 0x11D2, {0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C}};

/* Official systemd Boot Loader Interface Vendor GUID: 4a67b082-0a4c-41cf-b6c7-440b29bb8c4f */
static const EFI_GUID gSystemdLoaderGuid =
    {0x4A67B082, 0x0A4C, 0x41CF, {0xB6, 0xC7, 0x44, 0x0B, 0x29, 0xBB, 0x8C, 0x4F}};

static const EFI_GUID gEfiGraphicsOutputProtocolGuid =
    {0x9042A9DE, 0x23DC, 0x4A38, {0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A}};

static const EFI_GUID gEfiSimplePointerProtocolGuid =
    {0x31878C87, 0x0B75, 0x11D5, {0x9A, 0x4F, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D}};

static const EFI_GUID gEfiLoadedImageProtocolGuid =
    {0x5B1B31A1, 0x9562, 0x11D2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

static const EFI_GUID gEfiSimpleFileSystemProtocolGuid =
    {0x0964E5B22, 0x6459, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

static const EFI_GUID gEfiFileInfoGuid =
    {0x09576E92, 0x6D3F, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

static const EFI_GUID gEfiDevicePathProtocolGuid =
    {0x09576E91, 0x6D3F, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

static const EFI_GUID gEfiLoadFile2ProtocolGuid =
    {0x4006C0C1, 0xFCB3, 0x403E, {0x99, 0x6D, 0x4A, 0x6C, 0x87, 0x24, 0xE0, 0x6D}};

/* Linux EFI stub initrd device path vendor GUID */
static const EFI_GUID gLinuxInitrdMediaGuid =
    {0x5568E427, 0x68FC, 0x4F3D, {0xAC, 0x74, 0xCA, 0x55, 0x52, 0x31, 0xCC, 0x68}};

#endif /* FINN_BOOT_UEFI_ABI_H */

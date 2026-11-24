# SPDX-License-Identifier: GPL-2.0-or-later
"""fixpe.py DRIVER.SYS - set the PE checksum and check that every import is one that Windows 98 SE
provides (NTKERN's ntoskrnl subset, HAL, HIDCLASS)."""
import sys, pefile
W98 = {
 'ntoskrnl.exe': set('''ExAllocatePool ExAllocatePoolWithTag ExFreePool IoAllocateIrp IoFreeIrp IoBuildDeviceIoControlRequest
  IoCancelIrp IofCallDriver IofCompleteRequest KeInitializeEvent KeSetEvent KeWaitForSingleObject InterlockedIncrement
  InterlockedDecrement InterlockedExchange PoCallDriver PoStartNextPowerIrp KeInitializeSpinLock IoAcquireCancelSpinLock
  IoReleaseCancelSpinLock RtlWriteRegistryValue ExQueueWorkItem
  KeInitializeTimer KeInitializeDpc KeSetTimer KeCancelTimer'''.split()),
 'hal.dll': {'KfAcquireSpinLock', 'KfReleaseSpinLock'},
 'hidclass.sys': {'HidRegisterMinidriver'},
}
p = pefile.PE(sys.argv[1])
for sec in p.sections:                     # Windows 98 maps drivers 1:1 - every section needs its bytes in the file
    if sec.SizeOfRawData == 0 or sec.PointerToRawData != sec.VirtualAddress:
        sys.exit('section %s: raw data %#x at %#x, RVA %#x - not loadable by Windows 98' % (sec.Name.rstrip(b'\0').decode(), sec.SizeOfRawData, sec.PointerToRawData, sec.VirtualAddress))
for imp in p.DIRECTORY_ENTRY_IMPORT:        # module names as in Microsoft's own drivers (NTOSKRNL.EXE)
    name = imp.dll.decode()
    p.set_bytes_at_rva(imp.struct.Name, name.upper().encode())
bad = []
for imp in p.DIRECTORY_ENTRY_IMPORT:
    dll = imp.dll.decode().lower()
    for i in imp.imports:
        n = i.name.decode() if i.name else '#%d' % i.ordinal
        print('  import %s!%s' % (dll, n))
        if n not in W98.get(dll, ()): bad.append('%s!%s' % (dll, n))
p.OPTIONAL_HEADER.DllCharacteristics = 0x2000   # IMAGE_DLLCHARACTERISTICS_WDM_DRIVER, as in hidusb.sys
p.OPTIONAL_HEADER.CheckSum = p.generate_checksum()
p.write(sys.argv[1])
print('%s: %d bytes, sections %s' % (sys.argv[1], len(open(sys.argv[1], 'rb').read()),
      [s.Name.rstrip(b'\0').decode() for s in p.sections]))
if bad:
    sys.exit('imports not available on Windows 98: ' + ', '.join(bad))

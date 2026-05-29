#pragma once

class UObject;
class FArchive;

// Binary save entry points. Temporary free-function home — will move to
// UPackage::Save / UPackage::Load when UPackage exists. Call sites change
// from `SavePackage(Obj, Ar)` to `UPackage::Save(Obj, Ar)` at that point.
//
// Currently the only consumer of FLinker machinery is FObjectProperty (via
// FObjectPtr storage). Other property types handle their own wire formats
// and pass through unaffected.

void SavePackage(UObject* Root, FArchive& Out);
void LoadPackage(UObject* Root, FArchive& In);

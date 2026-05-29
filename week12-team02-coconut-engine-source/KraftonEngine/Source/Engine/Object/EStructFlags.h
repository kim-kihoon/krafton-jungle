#pragma once

// For future implementation
enum EStructFlags
{
	STRUCT_NoFlags = 0x00000000,
	STRUCT_Native = 0x00000001,
	STRUCT_IdenticalNative = 0x00000002,
	STRUCT_HasInstancedReference = 0x00000004,
	STRUCT_NoExport = 0x00000008,
	STRUCT_Atomic = 0x00000010,
	STRUCT_Immutable = 0x00000020,
	STRUCT_AddStructReferencedObjects = 0x00000040,
	STRUCT_RequiredAPI = 0x00000200,
	STRUCT_NetSerializeNative = 0x00000400,
	STRUCT_SerializeNative = 0x00000800,
	STRUCT_CopyNative = 0x00001000,
	STRUCT_IsPlainOldData = 0x00002000,
	STRUCT_NoDestructor = 0x00004000,
	STRUCT_ZeroConstructor = 0x00008000,
	STRUCT_ExportTextItemNative = 0x00010000,
	STRUCT_ImportTextItemNative = 0x00020000,
	STRUCT_PostSerializeNative = 0x00040000,
	STRUCT_SerializeFromMismatchedTag = 0x00080000,
	STRUCT_NetDeltaSerializeNative = 0x00100000,
	STRUCT_PostScriptConstruct = 0x00200000,
	STRUCT_NetSharedSerialization = 0x00400000,
	STRUCT_Trashed = 0x00800000,
	STRUCT_NewerVersionExists = 0x01000000,
	STRUCT_CanEditChange = 0x02000000,
	STRUCT_Visitor = 0x04000000,
	STRUCT_Inherit = STRUCT_HasInstancedReference | STRUCT_Atomic,
	STRUCT_ComputedFlags = STRUCT_NetDeltaSerializeNative | STRUCT_NetSerializeNative | STRUCT_SerializeNative | STRUCT_PostSerializeNative | STRUCT_CopyNative | STRUCT_IsPlainOldData | STRUCT_NoDestructor | STRUCT_ZeroConstructor | STRUCT_IdenticalNative | STRUCT_AddStructReferencedObjects | STRUCT_ExportTextItemNative | STRUCT_ImportTextItemNative | STRUCT_SerializeFromMismatchedTag | STRUCT_PostScriptConstruct | STRUCT_NetSharedSerialization,
};

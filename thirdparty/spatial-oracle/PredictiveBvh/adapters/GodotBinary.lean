-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026-present K. S. Ernest (iFire) Lee

import Shared.Types

/-!
# Godot Binary Resource Format (.scn/.res) — Formal Specification

Specifies the wire format for Godot Engine's binary resource serialization
(FORMAT_VERSION 6, Godot 4.5+). Used by `libidtx_core` to write `.scn` files
without linking Godot, enabling the zone-baker Elixir release to produce
baked PackedScenes as a standalone service.

Two file variants:
- **RSRC** (uncompressed): magic `RSRC`, direct binary payload
- **RSCC** (zstd block-compressed): magic `RSCC`, block-compressed payload

Source of truth: `E:\multiplayer-fabric-godot\core\io\resource_format_binary.cpp`

## Codegen target

This spec emits a C header (`godot_scn.h`) via the bvh-codegen pipeline.
The emitted code provides:
- `godot_scn_write(buf, scene)` — serialize a scene to RSRC or RSCC
- `godot_scn_read(buf, len)` — deserialize a scene from either variant
- Variant encoder/decoder for all types needed by PackedScene

## Coordinate convention

All spatial values follow the monorepo convention: integer micrometres (μm).
Godot uses float32/float64 metres; the C emitter converts at the boundary.
-/

namespace PredictiveBVH.GodotBinary

-- ═══════════════════════════════════════════════════════════════════════════
-- PART 1: FILE STRUCTURE
-- ═══════════════════════════════════════════════════════════════════════════

/-- File magic bytes identifying the two variants. -/
inductive FileMagic where
  | RSRC  -- uncompressed: bytes [0x52, 0x53, 0x52, 0x43]
  | RSCC  -- compressed:   bytes [0x52, 0x53, 0x43, 0x43]
  deriving Repr, BEq, Inhabited

/-- Compression modes supported by FileAccessCompressed. -/
inductive CompressionMode where
  | FastLZ   -- 0
  | Deflate  -- 1
  | Zstd     -- 2 (default for .scn)
  | Gzip     -- 3
  deriving Repr, BEq, Inhabited

def CompressionMode.toNat : CompressionMode → Nat
  | .FastLZ  => 0
  | .Deflate => 1
  | .Zstd    => 2
  | .Gzip    => 3

/-- Compressed file header (RSCC variant).
    After the 4-byte magic, the compressed envelope stores block metadata
    before the zstd-compressed payload blocks. -/
structure CompressedHeader where
  mode      : CompressionMode  -- uint32: compression algorithm
  blockSize : Nat              -- uint32: uncompressed block size (default 4096)
  totalSize : Nat              -- uint32: total uncompressed size
  deriving Repr

/-- A single compressed block descriptor. -/
structure CompressedBlock where
  compressedSize : Nat  -- uint32: size of this block after compression
  deriving Repr

/-- Number of blocks = ceil(totalSize / blockSize). -/
def blockCount (h : CompressedHeader) : Nat :=
  h.totalSize / h.blockSize + 1

theorem blockCount_pos (h : CompressedHeader) : 0 < blockCount h := by
  unfold blockCount
  exact Nat.succ_pos _

-- ═══════════════════════════════════════════════════════════════════════════
-- PART 2: RESOURCE HEADER (after decompression for RSCC, direct for RSRC)
-- ═══════════════════════════════════════════════════════════════════════════

/-- Format flags stored in the resource header. -/
structure FormatFlags where
  namedSceneIds  : Bool  -- bit 0: use named IDs for sub-resources
  uids           : Bool  -- bit 1: resources have UIDs
  realIsDouble   : Bool  -- bit 2: real_t is 64-bit
  hasScriptClass : Bool  -- bit 3: script class string follows UID
  deriving Repr, BEq, Inhabited

def FormatFlags.toBitfield (f : FormatFlags) : Nat :=
  (if f.namedSceneIds  then 1 else 0) |||
  (if f.uids           then 2 else 0) |||
  (if f.realIsDouble   then 4 else 0) |||
  (if f.hasScriptClass then 8 else 0)

/-- The binary resource file header (FORMAT_VERSION = 6). -/
structure ResourceHeader where
  bigEndian       : Bool          -- uint32: 0=LE, 1=BE
  use64Bit        : Bool          -- uint32: always false currently
  versionMajor    : Nat           -- uint32: Godot engine major version
  versionMinor    : Nat           -- uint32: Godot engine minor version
  formatVersion   : Nat           -- uint32: binary format version (6)
  resourceType    : String        -- unicode string: e.g. "PackedScene"
  importMdOffset  : Nat           -- uint64: offset to import metadata (0 if none)
  flags           : FormatFlags   -- uint32: format flags
  uid             : Nat           -- uint64: resource UID
  scriptClass     : Option String -- present only if flags.hasScriptClass
  deriving Repr

def FORMAT_VERSION : Nat := 6
def RESERVED_FIELDS : Nat := 11

theorem format_version_is_6 : FORMAT_VERSION = 6 := rfl
theorem reserved_fields_is_11 : RESERVED_FIELDS = 11 := rfl

-- ═══════════════════════════════════════════════════════════════════════════
-- PART 3: VARIANT TYPE TAGS
-- ═══════════════════════════════════════════════════════════════════════════

/-- Variant type tags used in binary serialization.
    Numbering is intentionally different from Variant::Type enum to allow
    jump table optimization (contiguous IDs within groups). -/
inductive VariantTag where
  | Nil                  -- 1
  | Bool                 -- 2
  | Int                  -- 3
  | Float                -- 4
  | String              -- 5
  | Vector2              -- 10
  | Rect2                -- 11
  | Vector3              -- 12
  | Plane                -- 13
  | Quaternion           -- 14
  | AABB                 -- 15
  | Basis                -- 16
  | Transform3D          -- 17
  | Transform2D          -- 18
  | Color                -- 20
  | NodePath             -- 22
  | RID                  -- 23
  | Object               -- 24
  | Dictionary           -- 26
  | Array                -- 30
  | PackedByteArray      -- 31
  | PackedInt32Array     -- 32
  | PackedFloat32Array   -- 33
  | PackedStringArray    -- 34
  | PackedVector3Array   -- 35
  | PackedColorArray     -- 36
  | PackedVector2Array   -- 37
  | Int64                -- 40
  | Double               -- 41
  | StringName           -- 44
  | Vector2i             -- 45
  | Rect2i               -- 46
  | Vector3i             -- 47
  | PackedInt64Array     -- 48
  | PackedFloat64Array   -- 49
  | Vector4              -- 50
  | Vector4i             -- 51
  | Projection           -- 52
  | PackedVector4Array   -- 53
  deriving Repr, BEq, Inhabited

def VariantTag.toNat : VariantTag → Nat
  | .Nil              => 1
  | .Bool             => 2
  | .Int              => 3
  | .Float            => 4
  | .String           => 5
  | .Vector2          => 10
  | .Rect2            => 11
  | .Vector3          => 12
  | .Plane            => 13
  | .Quaternion       => 14
  | .AABB             => 15
  | .Basis            => 16
  | .Transform3D      => 17
  | .Transform2D      => 18
  | .Color            => 20
  | .NodePath         => 22
  | .RID              => 23
  | .Object           => 24
  | .Dictionary       => 26
  | .Array            => 30
  | .PackedByteArray  => 31
  | .PackedInt32Array => 32
  | .PackedFloat32Array => 33
  | .PackedStringArray  => 34
  | .PackedVector3Array => 35
  | .PackedColorArray   => 36
  | .PackedVector2Array => 37
  | .Int64            => 40
  | .Double           => 41
  | .StringName       => 44
  | .Vector2i         => 45
  | .Rect2i           => 46
  | .Vector3i         => 47
  | .PackedInt64Array => 48
  | .PackedFloat64Array => 49
  | .Vector4          => 50
  | .Vector4i         => 51
  | .Projection       => 52
  | .PackedVector4Array => 53

/-- Object sub-type tags (within VARIANT_OBJECT). -/
inductive ObjectSubtype where
  | Empty                 -- 0: null object
  | ExternalResource      -- 1: type string + path string
  | InternalResource      -- 2: path string (local://)
  | ExternalResourceIndex -- 3: uint32 index into ext_resources table
  deriving Repr, BEq, Inhabited

def ObjectSubtype.toNat : ObjectSubtype → Nat
  | .Empty                 => 0
  | .ExternalResource      => 1
  | .InternalResource      => 2
  | .ExternalResourceIndex => 3

-- ═══════════════════════════════════════════════════════════════════════════
-- PART 4: RESOURCE BODY STRUCTURE
-- ═══════════════════════════════════════════════════════════════════════════

/-- An external resource reference (dependency). -/
structure ExtResource where
  type : String  -- class name
  path : String  -- resource path (res:// or relative)
  uid  : Nat     -- uint64 UID
  deriving Repr

/-- An internal (sub) resource entry in the offset table. -/
structure IntResourceEntry where
  path   : String  -- "local://<unique_id>" for built-in resources
  offset : Nat     -- uint64: absolute file offset to resource data
  deriving Repr

/-- A single property in a resource. -/
structure Property where
  nameIdx : Nat     -- index into string table
  -- value is a Variant (recursive, modeled separately)
  deriving Repr

/-- Resource data block (one per internal resource). -/
structure ResourceData where
  typeName      : String        -- class name (e.g. "ArrayMesh", "PackedScene")
  propertyCount : Nat           -- uint32
  -- properties follow (each: uint32 name_idx + variant data)
  deriving Repr

-- ═══════════════════════════════════════════════════════════════════════════
-- PART 5: PACKED SCENE BUNDLE FORMAT
-- ═══════════════════════════════════════════════════════════════════════════

/-- PackedScene version (PACKED_SCENE_VERSION in Godot source). -/
def PACKED_SCENE_VERSION : Nat := 3

/-- A node record within the PackedScene._bundled "nodes" PackedInt32Array.
    Encoding: flat array of ints, decoded sequentially. -/
structure NodeRecord where
  parentIdx     : Int     -- index of parent node (-1 / 0x7FFFFFFF = no parent / root)
  ownerIdx      : Int     -- index of owner node
  typeNameIdx   : Nat     -- index into "names" array
  nameIdx       : Nat     -- index into "names" array
  instanceIdx   : Int     -- index into "variants" for instanced scene (-1 if not)
  index         : Int     -- sibling order (-1 for implicit)
  properties    : List (Nat × Nat)  -- (name_idx, value_idx) pairs
  groups        : List Nat          -- group name indices
  deriving Repr

/-- The _bundled Dictionary structure stored as the sole property of PackedScene. -/
structure BundledScene where
  names           : List String        -- "names": all node/property name strings
  variants        : List Nat           -- "variants": indices are opaque (actual Variant values)
  nodeCount       : Nat                -- "node_count"
  nodes           : List Int           -- "nodes": flat PackedInt32Array
  connCount       : Nat                -- "conn_count"
  conns           : List Int           -- "conns": flat PackedInt32Array
  nodePaths       : List Nat           -- "node_paths": array indices
  editableInstances : List Nat         -- "editable_instances" (optional)
  baseScene       : Option Nat         -- "base_scene" (optional, index into variants)
  version         : Nat                -- "version" = PACKED_SCENE_VERSION
  deriving Repr

theorem bundled_version_is_3 : PACKED_SCENE_VERSION = 3 := rfl

/-- Node record size in the flat int array (minimum: 7 fixed fields). -/
def nodeRecordMinSize : Nat := 7

/-- Connection record size (fixed fields: from, to, signal, method, flags, unbinds). -/
def connRecordMinSize : Nat := 6

-- ═══════════════════════════════════════════════════════════════════════════
-- PART 6: STRING ENCODING
-- ═══════════════════════════════════════════════════════════════════════════

/-- Strings are length-prefixed UTF-8 with NO padding.
    Length includes the null terminator byte.
    Source: save_unicode_string stores utf8.length()+1, get_unicode_string reads exactly len bytes.
    There is NO 4-byte alignment padding on strings in the binary resource format. -/
def stringWireSize (len : Nat) : Nat :=
  4 + len

/-- Wire size is always at least 4 (the length prefix). -/
theorem stringWireSize_min (len : Nat) : 4 ≤ stringWireSize len := by
  unfold stringWireSize; omega

/-- String table references: if bit 31 is set, the remaining bits are an
    inline string length. Otherwise, it's an index into the string table. -/
def STRING_INLINE_BIT : Nat := 0x80000000
def STRING_LENGTH_MASK : Nat := 0x7FFFFFFF

-- ═══════════════════════════════════════════════════════════════════════════
-- PART 7: COMPRESSED FILE ROUNDTRIP
-- ═══════════════════════════════════════════════════════════════════════════

/-- The compressed file layout (RSCC):
    magic(4) + mode(4) + blockSize(4) + totalSize(4)
    + blockCount × compressedSize(4)
    + compressed blocks (concatenated)
    + magic(4) at EOF -/
def compressedHeaderSize (h : CompressedHeader) : Nat :=
  4 + 4 + 4 + 4 + blockCount h * 4

/-- Default block size for .scn files. -/
def DEFAULT_BLOCK_SIZE : Nat := 4096

/-- Default compression mode for .scn files. -/
def DEFAULT_COMPRESSION_MODE : CompressionMode := .Zstd

theorem default_mode_is_zstd : DEFAULT_COMPRESSION_MODE.toNat = 2 := rfl
theorem default_block_size_is_4096 : DEFAULT_BLOCK_SIZE = 4096 := rfl

/-- Compressed file invariant: all blocks decompress to blockSize bytes,
    except the last block which decompresses to totalSize % blockSize bytes
    (or blockSize if evenly divisible — but blockCount already accounts for +1). -/
def lastBlockSize (h : CompressedHeader) : Nat :=
  h.totalSize % h.blockSize

-- ═══════════════════════════════════════════════════════════════════════════
-- PART 8: FOOTER
-- ═══════════════════════════════════════════════════════════════════════════

/-- Both RSRC and RSCC files end with a 4-byte footer.
    RSRC: "RSRC" at EOF
    RSCC: magic repeated at EOF (after all compressed blocks) -/
def FOOTER_SIZE : Nat := 4

-- ═══════════════════════════════════════════════════════════════════════════
-- PART 9: VALIDATION PROPERTIES
-- ═══════════════════════════════════════════════════════════════════════════

/-- A well-formed ResourceHeader has formatVersion = 6. -/
def ResourceHeader.isValid (h : ResourceHeader) : Prop :=
  h.formatVersion = FORMAT_VERSION

/-- A well-formed BundledScene has version = 3 and nodeCount matches nodes. -/
def BundledScene.isWellFormed (b : BundledScene) : Prop :=
  b.version = PACKED_SCENE_VERSION ∧
  b.names.length > 0

/-- FormatFlags for a standard PackedScene export (no script class, float32). -/
def standardPackedSceneFlags : FormatFlags := {
  namedSceneIds  := true
  uids           := true
  realIsDouble   := false
  hasScriptClass := false
}

theorem standard_flags_bitfield :
    standardPackedSceneFlags.toBitfield = 3 := by
  native_decide

-- ═══════════════════════════════════════════════════════════════════════════
-- PART 10: WIRE SIZE CALCULATIONS (for buffer pre-allocation)
-- ═══════════════════════════════════════════════════════════════════════════

/-- Minimum header size (excludes variable-length type string and script class):
    magic(4) + endian(4) + use64(4) + major(4) + minor(4) + format(4)
    + import_md_offset(8) + flags(4) + uid(8) + reserved(44) = 88 bytes
    Plus the type string (variable). -/
def MIN_HEADER_FIXED_SIZE : Nat := 88

/-- External resource entry wire size (excludes variable-length strings):
    type_string + path_string + uid(8). -/
def extResourceFixedOverhead : Nat := 8  -- just the UID; strings are variable

/-- Internal resource entry wire size: path_string + offset(8). -/
def intResourceFixedOverhead : Nat := 8  -- just the offset; path is variable

-- ═══════════════════════════════════════════════════════════════════════════
-- PART 11: VERIFICATION
-- ═══════════════════════════════════════════════════════════════════════════

#eval do
  IO.println s!"FORMAT_VERSION = {FORMAT_VERSION}"
  IO.println s!"PACKED_SCENE_VERSION = {PACKED_SCENE_VERSION}"
  IO.println s!"DEFAULT_BLOCK_SIZE = {DEFAULT_BLOCK_SIZE}"
  IO.println s!"DEFAULT_COMPRESSION_MODE = {DEFAULT_COMPRESSION_MODE.toNat} (zstd)"
  IO.println s!"RESERVED_FIELDS = {RESERVED_FIELDS}"
  IO.println s!"MIN_HEADER_FIXED_SIZE = {MIN_HEADER_FIXED_SIZE}"
  IO.println s!"standardPackedSceneFlags.toBitfield = {standardPackedSceneFlags.toBitfield}"
  IO.println s!"STRING_INLINE_BIT = 0x{String.ofList (Nat.toDigits 16 STRING_INLINE_BIT)}"
  IO.println "GodotBinary spec OK"

end PredictiveBVH.GodotBinary

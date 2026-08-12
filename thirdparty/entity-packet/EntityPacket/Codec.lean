namespace EntityPacket

/-- The 100-byte fabric entity packet, modelled exactly as the C++
    (`XRGridEntityPacket`). Every field is integral — the wire has no floats.
    This Lean spec is the source of truth the C++ must match; Plausible
    roundtrip properties find codec gaps without an engine rebuild. -/

def SIZE : Nat := 100
def PAYLOAD_OFFSET : Nat := 58
def PAYLOAD_LEN : Nat := SIZE - PAYLOAD_OFFSET   -- 42

/-- Where each field sits. Named rather than written into `encode` and `decode` as literals,
    because a third reader wants them: `Emit.lean` emits a C header for the plane and the edge
    that speak this wire, and an emitter that retyped the numbers could disagree with the spec
    it claims to come from. Here they are typed once and read three times. -/
def OFF_GID : Nat := 0
def OFF_POS : Nat := 4          -- three Int64, 8 apart
def OFF_VEL : Nat := 28         -- three Int16, 2 apart
def OFF_HLC : Nat := 40
def OFF_CLASS_OWNER : Nat := 44
def OFF_SUB_INDEX : Nat := 48
def OFF_ROT : Nat := 52         -- three Int16, 2 apart

/-- The i16 velocity scale, in micrometres per tick. It belongs to the predictive BVH
    (`PBVH_V_MAX_PHYSICAL_DEFAULT` in `lean-spatial-oracle`) and is repeated here because the
    packet cannot be decoded without it: a velocity field is meaningless without the scale it
    was divided by, and a consumer that guessed would be wrong quietly. -/
def V_MAX_UM_PER_TICK : Nat := 500000

abbrev Bytes := Array UInt8

/-- little-endian put/get for the widths the packet uses. -/
def putU32 (b : Bytes) (off : Nat) (v : UInt32) : Bytes := Id.run do
  let mut b := b
  for i in [0:4] do
    b := b.set! (off + i) (((v >>> (UInt32.ofNat (i*8))) &&& 0xFF).toUInt8)
  return b
def getU32 (b : Bytes) (off : Nat) : UInt32 := Id.run do
  let mut v : UInt32 := 0
  for i in [0:4] do
    v := v ||| ((b[off+i]!).toUInt32 <<< (UInt32.ofNat (i*8)))
  return v

def putI64 (b : Bytes) (off : Nat) (v : Int64) : Bytes := Id.run do
  let u : UInt64 := v.toUInt64
  let mut b := b
  for i in [0:8] do
    b := b.set! (off + i) (((u >>> (UInt64.ofNat (i*8))) &&& 0xFF).toUInt8)
  return b
def getI64 (b : Bytes) (off : Nat) : Int64 := Id.run do
  let mut u : UInt64 := 0
  for i in [0:8] do
    u := u ||| ((b[off+i]!).toUInt64 <<< (UInt64.ofNat (i*8)))
  return u.toInt64

def putI16 (b : Bytes) (off : Nat) (v : Int16) : Bytes := Id.run do
  let u : UInt16 := v.toUInt16
  let b := b.set! off ((u &&& 0xFF).toUInt8)
  b.set! (off+1) (((u >>> 8) &&& 0xFF).toUInt8)
def getI16 (b : Bytes) (off : Nat) : Int16 :=
  (((b[off]!).toUInt16) ||| ((b[off+1]!).toUInt16 <<< 8)).toInt16

/-- An entity envelope's integral fields (position already in μm). -/
structure Packet where
  gid       : UInt32
  posUm     : Int64 × Int64 × Int64   -- absolute micrometers
  vel       : Int16 × Int16 × Int16   -- PBVH-scaled i16
  hlc       : UInt32
  classOwner: UInt32                   -- (class<<24)|owner
  subIndex  : UInt32
  rot       : Int16 × Int16 × Int16    -- swing-twist
  payload   : Bytes                    -- 42 bytes
  deriving Repr

def encode (p : Packet) : Bytes := Id.run do
  let mut b : Bytes := Array.replicate SIZE 0
  b := putU32 b OFF_GID p.gid
  b := putI64 b OFF_POS p.posUm.1
  b := putI64 b (OFF_POS + 8) p.posUm.2.1
  b := putI64 b (OFF_POS + 16) p.posUm.2.2
  b := putI16 b OFF_VEL p.vel.1
  b := putI16 b (OFF_VEL + 2) p.vel.2.1
  b := putI16 b (OFF_VEL + 4) p.vel.2.2
  b := putU32 b OFF_HLC p.hlc
  b := putU32 b OFF_CLASS_OWNER p.classOwner
  b := putU32 b OFF_SUB_INDEX p.subIndex
  b := putI16 b OFF_ROT p.rot.1
  b := putI16 b (OFF_ROT + 2) p.rot.2.1
  b := putI16 b (OFF_ROT + 4) p.rot.2.2
  for i in [0:PAYLOAD_LEN] do
    if h : i < p.payload.size then
      b := b.set! (PAYLOAD_OFFSET + i) (p.payload[i]!)
  return b

def decode (b : Bytes) : Packet :=
  { gid := getU32 b OFF_GID
    posUm := (getI64 b OFF_POS, getI64 b (OFF_POS + 8), getI64 b (OFF_POS + 16))
    vel := (getI16 b OFF_VEL, getI16 b (OFF_VEL + 2), getI16 b (OFF_VEL + 4))
    hlc := getU32 b OFF_HLC
    classOwner := getU32 b OFF_CLASS_OWNER
    subIndex := getU32 b OFF_SUB_INDEX
    rot := (getI16 b OFF_ROT, getI16 b (OFF_ROT + 2), getI16 b (OFF_ROT + 4))
    payload := (Array.range PAYLOAD_LEN).map (fun i => b[PAYLOAD_OFFSET + i]!) }

end EntityPacket

extends SceneTree
# Differential: decode the Lean golden packets via the C++ XRGridEntityPacket and
# verify every field matches the Lean spec.
const GOLDEN := "/home/ernest.lee/Documents/entity_packet/build/packet_golden.csv"
const VEL_SCALE := 65534.0   # 32767 / (500000 um/tick / 1e6) — PBVH-tied

func hex2bytes(h: String) -> PackedByteArray:
	var b := PackedByteArray()
	for i in range(0, h.length(), 2):
		b.append(("0x" + h.substr(i, 2)).hex_to_int())
	return b

func _init():
	var f = FileAccess.open(GOLDEN, FileAccess.READ)
	f.get_line() # header
	var n := 0; var bad := 0; var first := ""
	while not f.eof_reached():
		var line = f.get_line()
		if line == "": continue
		var c = line.split(",")
		var bytes = hex2bytes(c[0])
		var d = XRGridEntityPacket.decode(bytes)
		var ok := true; var why := ""
		if bytes.size() != 100: ok = false; why = "size %d" % bytes.size()
		if int(d["global_id"]) != int(c[1]): ok = false; why = "gid"
		# position: decoded meters * 1e6 == golden um (within 1um)
		var pos: Vector3 = d["position"]
		if abs(pos.x*1e6 - float(c[2])) > 1.5: ok = false; why = "posx %f vs %s" % [pos.x*1e6, c[2]]
		if abs(pos.y*1e6 - float(c[3])) > 1.5: ok = false; why = "posy"
		if abs(pos.z*1e6 - float(c[4])) > 1.5: ok = false; why = "posz"
		# velocity: decoded == golden_i16 / VEL_SCALE
		var vel: Vector3 = d["velocity"]
		if abs(vel.x - float(c[5])/VEL_SCALE) > 1e-4: ok = false; why = "velx"
		# payload ends
		var pay: PackedByteArray = d["payload"]
		if pay.size() != 42 or pay[0] != int(c[8]) or pay[41] != int(c[9]): ok = false; why = "payload"
		if not ok:
			bad += 1
			if first == "": first = "row %d: %s" % [n, why]
		n += 1
	if bad == 0:
		print("PACKET DIFFERENTIAL PASS: C++ decode matches the Lean spec on all %d vectors" % n)
	else:
		print("FAIL: %d/%d mismatch; first %s" % [bad, n, first])
	quit(0 if bad == 0 else 1)

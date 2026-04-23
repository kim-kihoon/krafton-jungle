import math

def multiply(A, B):
    ret = [[0]*4 for _ in range(4)]
    for i in range(4):
        for j in range(4):
            for k in range(4):
                ret[i][j] += A[i][k] * B[k][j]
    return ret

def yaw(deg):
    rad = math.radians(deg)
    c, s = math.cos(rad), math.sin(rad)
    return [
        [c, s, 0, 0],
        [-s, c, 0, 0],
        [0, 0, 1, 0],
        [0, 0, 0, 1]
    ]

def pitch(deg):
    rad = math.radians(deg)
    c, s = math.cos(rad), math.sin(rad)
    return [
        [c, 0, -s, 0],
        [0, 1, 0, 0],
        [s, 0, c, 0],
        [0, 0, 0, 1]
    ]

def transform(v, M):
    return [
        v[0]*M[0][0] + v[1]*M[1][0] + v[2]*M[2][0] + 1*M[3][0],
        v[0]*M[0][1] + v[1]*M[1][1] + v[2]*M[2][1] + 1*M[3][1],
        v[0]*M[0][2] + v[1]*M[1][2] + v[2]*M[2][2] + 1*M[3][2]
    ]

# Pole is at Z axis
v = [0, 0, 1]

print("M_1/0 * M_1 (Yaw first, then Pitch)")
for t in [0, 90, 180, 270]:
    M_local = yaw(t)
    M_parent = pitch(23.5)
    M_world = multiply(M_local, M_parent)
    v_out = transform(v, M_world)
    print(f"t={t}: {v_out}")

print("\nM_1 * M_1/0 (Pitch first, then Yaw)")
for t in [0, 90, 180, 270]:
    M_local = yaw(t)
    M_parent = pitch(23.5)
    M_world = multiply(M_parent, M_local)
    v_out = transform(v, M_world)
    print(f"t={t}: {v_out}")


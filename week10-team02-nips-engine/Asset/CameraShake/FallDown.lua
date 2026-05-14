-- Camera Shake Asset: FallDown (one-shot)
return {
    Modifiers = {
        {
            Type         = "Shake",
            Duration     = 3.0,

            RotAmplitude = { 0.3,   0.1,   0.3   },
            RotFrequency = { 5.0,   5.0,   3.0   },
            RotBezierCP  = { 0.0,   0.0,   1.0,   0.0,   1.0,   1.0   },

            LocAmplitude = { 0.0,   0.0,   0.0   },
            LocFrequency = { 15.0,  15.0,  15.0  },
            LocBezierCP  = { 0.25,  0.1,   0.75,  0.9,   1.0,   0.0   },

            FOVAmplitude = 0.0,
            FOVFrequency = 15.0,
            FOVBezierCP  = { 0.25,  0.1,   0.75,  0.9,   1.0,   0.0   },
        },
    },
}

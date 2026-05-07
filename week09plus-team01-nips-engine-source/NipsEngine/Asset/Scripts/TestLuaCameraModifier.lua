return {
    Modifiers = {
        {
            Type = "Camera",

            -- FOV is stored in radians.
            FOVOffset = {
                Base = 0.0,
                Amplitude = 0.03,
                Frequency = 4.0,
            },
        },

        {
            Type = "PostProcess",

            Gamma = {
                From = 1.0,
                To = 2.4,
                Duration = 2.0,
                Loop = true,
                PingPong = true,
            },

            VignetteIntensity = 1.0,
            VignetteRadius = 0.35,
            VignetteSoftness = 0.45,
        },

        {
            Type = "Overlay",

            FadeColor = { R = 0.0, G = 0.0, B = 0.0, A = 0.15 },
        },

        {
            Type = "LetterBox",
            Id = "IntroLetterBox",

            TargetRatio = 0.08,
            Duration = 1.0,
        },

        {
            Type = "Fade",
            Id = "IntroFade",

            Color = { R = 0.0, G = 0.0, B = 0.0, A = 1.0 },
            FromAlpha = 0.0,
            ToAlpha = 0.25,
            Duration = 1.0,
            Hold = true,
        },

        {
            Type = "Shake",
            Id = "IntroShake",

            Duration = 0.5,
            RotAmplitude = { 0.02, 0.0, 0.0 },
            RotFrequency = { 15.0, 15.0, 15.0 },
            FOVAmplitude = 0.01,
            FOVFrequency = 12.0,
        },
    },
}

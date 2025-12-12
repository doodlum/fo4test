Scriptname Upscaling:MCM Hidden Const
{
	@author bp42s
}


; #### IMPORTS ####



; #### VARIABLES ####



; #### PROPERTIES ####



; #### FUNCTIONS ####
;
;/
; Resets all MCM settings to their default values.
/;
Function ResetSettings() Global
	; NVIDIA DLSS 4 is automatically used if available, otherwise AMD FSR 3.1 is used
	; Default: 2 (0 = TAA, 1 = FSR, 2 = DLSS)
	MCM.SetModSettingInt("Upscaling", "iUpscaleMethodPreference:Settings", 2)

	; Controls the internal resolution multiplier.
	; Default: 1 (0=Native AA, 1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance)
	MCM.SetModSettingInt("Upscaling", "iQualityMode:Settings", 1)

	; Boosts image clarity using AMD Robust Contrast Adaptive Sharpening.
	; Default: 0.5 (0.0 to 1.0)
	MCM.SetModSettingFloat("Upscaling", "fSharpness:Settings", 0.5)

	MCM.RefreshMenu()
EndFunction



; #### EVENTS ####

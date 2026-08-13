#include "Core/HooksInternal.h"
#include "Core/DiagnosticsFormatter.h"
#include "Core/ShaderCache.h"

#include <format>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace CommunityShaders::Hooks
{
#if defined(FALLOUT_PRE_NG)
	namespace
	{
		// LLFPixelTracker domain: state, constants, and internal helpers (moved from Hooks.cpp).

		std::unordered_map<ID3D11PixelShader*, ShaderCache::ShaderMetadata> llfCandidatePixelShaders;
		std::unordered_set<std::string> loggedLLFPixelCandidates;
		std::unordered_set<std::string> loggedLLFDrawHookKinds;
		std::unordered_set<std::string> loggedLLFContextHookKinds;
		std::unordered_set<std::string> loggedLLFDrawContexts;
		std::unordered_set<std::string> loggedLLFContextDiagnostics;
		std::unordered_set<std::string> loggedLLFPixelShaderBindings;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundPixelShaders;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundObservedPixelShaders;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundTrackedPixelShaders;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundTargetPixelShaders;
		std::unordered_set<ID3D11PixelShader*> countedLLFBoundUnknownPixelShaders;
		std::unordered_set<ID3D11PixelShader*> loggedLLFUnknownPixelShaderBindings;
		std::unordered_set<std::string> loggedLLFBoundPixelShaderSurvey;
		std::unordered_set<std::string> countedLLFBoundPixelShaderSurvey;
		std::unordered_set<std::string> loggedLLFBoundPixelShaderSurveyReasons;
		std::unordered_set<std::string> loggedLLFBoundPixelShaderSurveyDraws;
		std::unordered_set<std::string> loggedLLFBoundPixelShaderNearTargetDraws;
		std::unordered_map<std::string, std::size_t> llfBoundPixelShaderSurveyReasonCounts;
		bool loggedLLFBoundPixelShaderSurveyLimit = false;
		bool loggedLLFBoundPixelShaderSurveyDrawLimit = false;
		bool loggedLLFBoundPixelShaderNearTargetDrawLimit = false;
		std::unordered_map<std::string, std::size_t> llfStateSnapshotCounts;
		std::unordered_map<ID3D11PixelShader*, ShaderCache::ShaderMetadata> observedPixelShaderMetadata;
		std::unordered_map<ID3D11DeviceContext*, ShaderCache::ShaderMetadata> llfBoundPixelShaderMetadataByContext;

		constexpr std::size_t kMaxLLFStateSnapshotsPerShaderKind = 8;
		constexpr std::size_t kMaxLLFBoundPixelShaderSurveyLogs = 96;
		constexpr std::size_t kLLFBoundPixelShaderSurveySummaryInterval = 128;
		constexpr std::size_t kMaxLLFUnknownPixelShaderBindingLogs = 8;
		constexpr std::size_t kMaxLLFBoundPixelShaderSurveyDrawLogs = 128;
		constexpr std::size_t kMaxLLFBoundPixelShaderNearTargetDrawLogs = 64;
		constexpr std::size_t kLLFBoundPixelShaderInventorySummaryInterval = 128;

		bool ShouldTraceLLFPixelCandidateDiagnostics()
		{
			static const bool enabled = ReadPreNGEnvironmentSwitch(kTraceLLFPSEnv);
			return enabled;
		}

		bool IsLightLimitFixPixelCandidate(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			return hasLightCB &&
			       hasLightingTexture &&
			       HasTextureDimension(a_metadata, 5, 5) &&
			       a_metadata.textureSampleCounts[5] > 0 &&
			       a_metadata.outputCount == 2 &&
			       positionOnlyInput &&
			       !a_metadata.hasDiscard &&
			       !a_metadata.hasImmediateConstantBuffer &&
			       a_metadata.immediateConstantBufferRows == 0;
		}

		bool IsLightLimitFixPixelImmediateConstantNearTarget(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			return hasLightCB &&
			       hasLightingTexture &&
			       HasTextureDimension(a_metadata, 5, 5) &&
			       a_metadata.textureSampleCounts[5] > 0 &&
			       a_metadata.outputCount == 2 &&
			       positionOnlyInput &&
			       !a_metadata.hasDiscard &&
			       (a_metadata.hasImmediateConstantBuffer || a_metadata.immediateConstantBufferRows != 0);
		}


		bool IsLightLimitFixPixelSurveyMatch(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			return hasLightCB && hasLightingTexture && a_metadata.outputCount == 2;
		}

		std::string FormatLightLimitFixPixelShape(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			const bool hasTextureCubeAtSlot5 = HasTextureDimension(a_metadata, 5, 5);
			const bool hasTwoOutputs = a_metadata.outputCount == 2;
			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			return std::format(
				"cb2={} t5={} tex5dim={} out2={} posInput={} target={} tracked={} samples={} t5samples={} immRows={}",
				hasLightCB,
				hasLightingTexture,
				hasTextureCubeAtSlot5,
				hasTwoOutputs,
				positionOnlyInput,
				IsLightLimitFixPixelCandidate(a_metadata),
				IsLightLimitFixPixelTrackedCandidate(a_metadata),
				a_metadata.sampleInstructionCount,
				a_metadata.textureSampleCounts[5],
				a_metadata.immediateConstantBufferRows);
		}

		std::string ClassifyLightLimitFixSurveyRejection(const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (IsLightLimitFixPixelCandidate(a_metadata)) {
				return "target";
			}

			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			if (!hasLightCB) {
				return "noCB2";
			}

			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			if (!hasLightingTexture) {
				return "noT5";
			}

			if (!HasTextureDimension(a_metadata, 5, 5)) {
				return "noT5Cube";
			}

			if (a_metadata.textureSampleCounts[5] == 0) {
				return "noT5Sample";
			}

			if (a_metadata.outputCount != 2) {
				return "output";
			}

			if (a_metadata.inputCount != 1 || a_metadata.inputMask != 0x1) {
				return "input";
			}

			if (a_metadata.hasDiscard) {
				return "discard";
			}

			if (a_metadata.hasImmediateConstantBuffer || a_metadata.immediateConstantBufferRows != 0) {
				return "immediateCB";
			}

			return "other";
		}

		std::string FormatLightLimitFixSurveyReasonCountsLocked()
		{
			constexpr std::string_view kReasonOrder[] = {
				"noCB2",
				"noT5",
				"noT5Cube",
				"noT5Sample",
				"output",
				"input",
				"discard",
				"immediateCB",
				"other"
			};

			std::ostringstream result;
			bool first = true;
			for (const auto reason : kReasonOrder) {
				const auto it = llfBoundPixelShaderSurveyReasonCounts.find(std::string(reason));
				if (it == llfBoundPixelShaderSurveyReasonCounts.end() || it->second == 0) {
					continue;
				}

				if (!first) {
					result << ',';
				}
				result << reason << ':' << it->second;
				first = false;
			}

			return first ? "none" : result.str();
		}

		bool ShouldSurveyBoundPixelShader(const ShaderCache::ShaderMetadata& a_metadata)
		{
			const bool hasLightCB = a_metadata.constantBufferSizes[2] > 0;
			const bool hasLightingTexture = (a_metadata.textureSlotMask & (1u << 5)) != 0;
			const bool hasTwoOutputs = a_metadata.outputCount == 2;
			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			return IsLightLimitFixPixelSurveyMatch(a_metadata) ||
			       hasLightCB ||
			       hasLightingTexture ||
			       (hasTwoOutputs && positionOnlyInput);
		}

		std::string ClassifyLightLimitFixPixelCandidate(const ShaderCache::ShaderMetadata& a_metadata)
		{
			if (IsLightLimitFixPixelCandidate(a_metadata)) {
				return "fullscreen-lighting";
			}

			if (IsLightLimitFixPixelImmediateConstantNearTarget(a_metadata)) {
				return "fullscreen-lighting-immediate";
			}

			const bool positionOnlyInput = a_metadata.inputCount == 1 && a_metadata.inputMask == 0x1;
			if (!positionOnlyInput || a_metadata.hasDiscard) {
				return "geometry-material";
			}

			if (a_metadata.hasImmediateConstantBuffer) {
				return "fullscreen-other";
			}

			return HasTextureDimension(a_metadata, 5, 5) ? "fullscreen-lighting" : "fullscreen-other";
		}

		std::size_t CountTrackedLightLimitFixPixelTargetsLocked()
		{
			std::size_t result = 0;
			for (const auto& [shader, metadata] : llfCandidatePixelShaders) {
				if (IsLightLimitFixPixelCandidate(metadata)) {
					++result;
				}
			}

			return result;
		}

		std::optional<ShaderCache::ShaderMetadata> GetTrackedLightLimitFixPixelShader(ID3D11PixelShader* a_pixelShader)
		{
			std::scoped_lock lock(llfCandidateLock);
			if (auto it = llfCandidatePixelShaders.find(a_pixelShader); it != llfCandidatePixelShaders.end()) {
				return it->second;
			}

			return std::nullopt;
		}

		std::optional<ShaderCache::ShaderMetadata> GetObservedPixelShader(ID3D11PixelShader* a_pixelShader)
		{
			std::scoped_lock lock(llfCandidateLock);
			if (auto it = observedPixelShaderMetadata.find(a_pixelShader); it != observedPixelShaderMetadata.end()) {
				return it->second;
			}

			return std::nullopt;
		}

		std::optional<ShaderCache::ShaderMetadata> GetBoundLightLimitFixPixelShader(ID3D11DeviceContext* a_context)
		{
			if (!a_context) {
				return std::nullopt;
			}

			{
				std::scoped_lock lock(llfCandidateLock);
				if (auto it = llfBoundPixelShaderMetadataByContext.find(a_context); it != llfBoundPixelShaderMetadataByContext.end()) {
					return it->second;
				}
			}

			winrt::com_ptr<ID3D11PixelShader> pixelShader;
			a_context->PSGetShader(pixelShader.put(), nullptr, nullptr);
			if (!pixelShader) {
				return std::nullopt;
			}

			return GetTrackedLightLimitFixPixelShader(pixelShader.get());
		}

	}

	// LLFPixelTracker domain (Promotion Step 1): definitions promoted out of the
	// anonymous namespace (declared in Core/HooksInternal.h).

	void TrackLightLimitFixBoundPixelShader(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
	{
		if (!a_context) {
			return;
		}

		auto metadata = a_pixelShader ? GetTrackedLightLimitFixPixelShader(a_pixelShader) : std::nullopt;
		std::scoped_lock lock(llfCandidateLock);
		if (metadata) {
			llfBoundPixelShaderMetadataByContext[a_context] = *metadata;
		} else {
			llfBoundPixelShaderMetadataByContext.erase(a_context);
		}
	}

	bool HasCachedBoundLightLimitFixPixelShader(ID3D11DeviceContext* a_context)
	{
		if (!a_context || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
			return false;
		}

		std::scoped_lock lock(llfCandidateLock);
		return llfBoundPixelShaderMetadataByContext.find(a_context) != llfBoundPixelShaderMetadataByContext.end();
	}

	void TraceLightLimitFixDrawHookHealth(const char* a_drawKind)
	{
		auto* cache = ShaderCache::GetSingleton();
		if (!ShouldTraceLLFPixelCandidates(*cache)) {
			return;
		}

		std::size_t trackedCandidateCount = 0;
		std::size_t trackedTargetCount = 0;
		{
			std::scoped_lock lock(llfCandidateLock);
			if (!loggedLLFDrawHookKinds.insert(a_drawKind).second) {
				return;
			}
			trackedCandidateCount = llfCandidatePixelShaders.size();
			trackedTargetCount = CountTrackedLightLimitFixPixelTargetsLocked();
		}

		logger::info(
			"[LightLimitFix] PreNG draw hook reached draw={} trackedCandidatePS={} trackedTargetPS={}",
			a_drawKind,
			trackedCandidateCount,
			trackedTargetCount);
	}

	void TraceLightLimitFixContextHookHealth(ID3D11DeviceContext* a_context, const char* a_hookKind)
	{
		auto* cache = ShaderCache::GetSingleton();
		if (!ShouldTraceLLFPixelCandidates(*cache)) {
			return;
		}

		std::size_t trackedCandidateCount = 0;
		std::size_t trackedTargetCount = 0;
		const auto key = std::format("{}:{:X}:{:X}", a_hookKind, ToAddress(a_context), GetContextVTablePointer(a_context));
		{
			std::scoped_lock lock(llfCandidateLock);
			if (!loggedLLFContextHookKinds.insert(key).second) {
				return;
			}
			trackedCandidateCount = llfCandidatePixelShaders.size();
			trackedTargetCount = CountTrackedLightLimitFixPixelTargetsLocked();
		}

		logger::info(
			"[LightLimitFix] PreNG context hook reached kind={} context=0x{:X} vtable=0x{:X} trackedCandidatePS={} trackedTargetPS={}",
			a_hookKind,
			ToAddress(a_context),
			GetContextVTablePointer(a_context),
			trackedCandidateCount,
			trackedTargetCount);
	}

	void TraceLightLimitFixPixelShaderBinding(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
	{
		if (!a_context || !a_pixelShader || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
			return;
		}

		auto metadata = GetTrackedLightLimitFixPixelShader(a_pixelShader);
		if (!metadata) {
			return;
		}

		const auto category = ClassifyLightLimitFixPixelCandidate(*metadata);
		const auto key = std::format("{}:{:08X}:{:X}:{:X}", metadata->uid, metadata->hash, ToAddress(a_context), ToAddress(a_pixelShader));
		{
			std::scoped_lock lock(llfCandidateLock);
			if (!loggedLLFPixelShaderBindings.insert(key).second) {
				return;
			}
		}

		logger::info(
			"[LightLimitFix] Candidate PS bound asmHash=0x{:08X} hash=0x{:08X} uid={} category={} target={} context=0x{:X} vtable=0x{:X} shader=0x{:X} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
			metadata->asmHash,
			metadata->hash,
			metadata->uid,
			category,
			IsLightLimitFixPixelCandidate(*metadata),
			ToAddress(a_context),
			GetContextVTablePointer(a_context),
			ToAddress(a_pixelShader),
			FormatBufferSlots(*metadata),
			FormatTextureSlots(*metadata),
			FormatTextureDimensions(*metadata),
			metadata->instructionCount,
			metadata->sampleInstructionCount,
			FormatTextureSampleCounts(*metadata),
			metadata->hasDiscard,
			metadata->hasImmediateConstantBuffer,
			metadata->immediateConstantBufferRows);
	}

	void TraceLightLimitFixBoundPixelShaderInventory(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
	{
		if (!a_context || !a_pixelShader || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
			return;
		}

		auto metadata = GetObservedPixelShader(a_pixelShader);
		const bool latestObserved = metadata.has_value();
		const bool latestTracked = metadata && IsLightLimitFixPixelTrackedCandidate(*metadata);
		const bool latestTarget = metadata && IsLightLimitFixPixelCandidate(*metadata);

		bool shouldLogSummary = false;
		bool shouldLogUnknownSample = false;
		std::size_t boundUniqueCount = 0;
		std::size_t observedBoundCount = 0;
		std::size_t unknownBoundCount = 0;
		std::size_t trackedBoundCount = 0;
		std::size_t targetBoundCount = 0;
		std::size_t observedCreatedCount = 0;
		std::size_t trackedCreatedCount = 0;
		std::size_t targetCreatedCount = 0;
		{
			std::scoped_lock lock(llfCandidateLock);
			const bool inserted = countedLLFBoundPixelShaders.insert(a_pixelShader).second;
			if (latestObserved) {
				countedLLFBoundObservedPixelShaders.insert(a_pixelShader);
			} else {
				countedLLFBoundUnknownPixelShaders.insert(a_pixelShader);
				if (loggedLLFUnknownPixelShaderBindings.size() < kMaxLLFUnknownPixelShaderBindingLogs &&
					loggedLLFUnknownPixelShaderBindings.insert(a_pixelShader).second) {
					shouldLogUnknownSample = true;
				}
			}

			if (latestTracked) {
				countedLLFBoundTrackedPixelShaders.insert(a_pixelShader);
			}

			if (latestTarget) {
				countedLLFBoundTargetPixelShaders.insert(a_pixelShader);
			}

			boundUniqueCount = countedLLFBoundPixelShaders.size();
			observedBoundCount = countedLLFBoundObservedPixelShaders.size();
			unknownBoundCount = countedLLFBoundUnknownPixelShaders.size();
			trackedBoundCount = countedLLFBoundTrackedPixelShaders.size();
			targetBoundCount = countedLLFBoundTargetPixelShaders.size();
			observedCreatedCount = observedPixelShaderMetadata.size();
			trackedCreatedCount = llfCandidatePixelShaders.size();
			targetCreatedCount = CountTrackedLightLimitFixPixelTargetsLocked();

			shouldLogSummary = inserted &&
				(boundUniqueCount == 1 ||
				 boundUniqueCount == 32 ||
				 boundUniqueCount == 64 ||
				 boundUniqueCount % kLLFBoundPixelShaderInventorySummaryInterval == 0 ||
				 latestTracked ||
				 latestTarget);
		}

		if (shouldLogUnknownSample) {
			logger::info(
				"[LightLimitFix] Bound PS unknown sample uniqueUnknown={} context=0x{:X} vtable=0x{:X} shader=0x{:X}",
				unknownBoundCount,
				ToAddress(a_context),
				GetContextVTablePointer(a_context),
				ToAddress(a_pixelShader));
		}

		if (!shouldLogSummary) {
			return;
		}

		logger::info(
			"[LightLimitFix] Bound PS inventory unique={} observed={} unknown={} trackedBound={} targetBound={} observedCreated={} trackedCreated={} targetCreated={} latestObserved={} latestTracked={} latestTarget={} context=0x{:X} vtable=0x{:X} shader=0x{:X}",
			boundUniqueCount,
			observedBoundCount,
			unknownBoundCount,
			trackedBoundCount,
			targetBoundCount,
			observedCreatedCount,
			trackedCreatedCount,
			targetCreatedCount,
			latestObserved,
			latestTracked,
			latestTarget,
			ToAddress(a_context),
			GetContextVTablePointer(a_context),
			ToAddress(a_pixelShader));
	}

	void TraceLightLimitFixBoundPixelShaderSurvey(ID3D11DeviceContext* a_context, ID3D11PixelShader* a_pixelShader)
	{
		if (!a_context || !a_pixelShader || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
			return;
		}

		auto metadata = GetObservedPixelShader(a_pixelShader);
		if (!metadata || IsLightLimitFixPixelTrackedCandidate(*metadata) || !ShouldSurveyBoundPixelShader(*metadata)) {
			return;
		}

		const auto key = std::format("{}:{:08X}:{:X}", metadata->uid, metadata->hash, ToAddress(a_pixelShader));
		const auto reason = ClassifyLightLimitFixSurveyRejection(*metadata);
		bool limitReached = false;
		bool shouldLogReasonSample = false;
		bool shouldLogSummary = false;
		bool shouldLog = false;
		std::size_t uniqueSurveyCount = 0;
		std::size_t exampleCount = 0;
		std::string reasonCounts;
		{
			std::scoped_lock lock(llfCandidateLock);
			if (!countedLLFBoundPixelShaderSurvey.insert(key).second) {
				return;
			}

			++llfBoundPixelShaderSurveyReasonCounts[reason];
			uniqueSurveyCount = countedLLFBoundPixelShaderSurvey.size();

			if (loggedLLFBoundPixelShaderSurveyReasons.insert(reason).second) {
				shouldLogReasonSample = true;
			}

			if (loggedLLFBoundPixelShaderSurvey.size() < kMaxLLFBoundPixelShaderSurveyLogs) {
				loggedLLFBoundPixelShaderSurvey.insert(key);
				shouldLog = true;
			} else if (!loggedLLFBoundPixelShaderSurveyLimit) {
				loggedLLFBoundPixelShaderSurveyLimit = true;
				limitReached = true;
			}

			exampleCount = loggedLLFBoundPixelShaderSurvey.size();
			if (uniqueSurveyCount == kMaxLLFBoundPixelShaderSurveyLogs + 1 ||
				(uniqueSurveyCount > kMaxLLFBoundPixelShaderSurveyLogs && uniqueSurveyCount % kLLFBoundPixelShaderSurveySummaryInterval == 0)) {
				shouldLogSummary = true;
				reasonCounts = FormatLightLimitFixSurveyReasonCountsLocked();
			}
		}

		if (shouldLogSummary) {
			logger::info(
				"[LightLimitFix] Bound PS survey reason summary unique={} examplesLogged={} counts={}",
				uniqueSurveyCount,
				exampleCount,
				reasonCounts);
		}

		if (shouldLogReasonSample && !shouldLog) {
			logger::info(
				"[LightLimitFix] Bound PS survey reason sample reason={} asmHash=0x{:08X} hash=0x{:08X} uid={} shape=\"{}\" buffers={} textures={} textureDims={} inputCount={} outputCount={} inputMask=0x{:X} outputMask=0x{:X} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
				reason,
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				FormatLightLimitFixPixelShape(*metadata),
				FormatBufferSlots(*metadata),
				FormatTextureSlots(*metadata),
				FormatTextureDimensions(*metadata),
				metadata->inputCount,
				metadata->outputCount,
				metadata->inputMask,
				metadata->outputMask,
				metadata->instructionCount,
				metadata->sampleInstructionCount,
				FormatTextureSampleCounts(*metadata),
				metadata->hasDiscard,
				metadata->hasImmediateConstantBuffer,
				metadata->immediateConstantBufferRows);
		}

		if (limitReached) {
			logger::info(
				"[LightLimitFix] Bound PS survey limit reached; suppressing additional noncandidate bound shader summaries limit={} unique={} counts={}",
				kMaxLLFBoundPixelShaderSurveyLogs,
				uniqueSurveyCount,
				reasonCounts.empty() ? "none" : reasonCounts);
		}
		if (!shouldLog) {
			return;
		}

		logger::info(
			"[LightLimitFix] Bound PS survey asmHash=0x{:08X} hash=0x{:08X} uid={} candidate=false reason={} shape=\"{}\" context=0x{:X} vtable=0x{:X} shader=0x{:X} buffers={} textures={} textureDims={} inputCount={} outputCount={} inputMask=0x{:X} outputMask=0x{:X} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
			metadata->asmHash,
			metadata->hash,
			metadata->uid,
			reason,
			FormatLightLimitFixPixelShape(*metadata),
			ToAddress(a_context),
			GetContextVTablePointer(a_context),
			ToAddress(a_pixelShader),
			FormatBufferSlots(*metadata),
			FormatTextureSlots(*metadata),
			FormatTextureDimensions(*metadata),
			metadata->inputCount,
			metadata->outputCount,
			metadata->inputMask,
			metadata->outputMask,
			metadata->instructionCount,
			metadata->sampleInstructionCount,
			FormatTextureSampleCounts(*metadata),
			metadata->hasDiscard,
			metadata->hasImmediateConstantBuffer,
			metadata->immediateConstantBufferRows);
	}

	void TraceLightLimitFixStateContext(ID3D11DeviceContext* a_context, const char* a_stateKind, std::string_view a_stateDetails)
	{
		if (!a_context || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
			return;
		}

		auto metadata = GetBoundLightLimitFixPixelShader(a_context);
		if (!metadata) {
			return;
		}

		const auto category = ClassifyLightLimitFixPixelCandidate(*metadata);
		const auto snapshotKey = std::format("{:08X}:{}:{}", metadata->asmHash, category, a_stateKind);
		std::size_t snapshotCount = 0;
		{
			std::scoped_lock lock(llfCandidateLock);
			snapshotCount = ++llfStateSnapshotCounts[snapshotKey];
		}
		if (snapshotCount > kMaxLLFStateSnapshotsPerShaderKind) {
			if (snapshotCount == kMaxLLFStateSnapshotsPerShaderKind + 1) {
				logger::info(
					"[LightLimitFix] Candidate PS state limit reached asmHash=0x{:08X} uid={} category={} target={} kind={} limit={}",
					metadata->asmHash,
					metadata->uid,
					category,
					IsLightLimitFixPixelCandidate(*metadata),
					a_stateKind,
					kMaxLLFStateSnapshotsPerShaderKind);
			}
			return;
		}

		D3D11_PRIMITIVE_TOPOLOGY topology{};
		a_context->IAGetPrimitiveTopology(&topology);

		D3D11_VIEWPORT viewport{};
		UINT viewportCount = 1;
		a_context->RSGetViewports(&viewportCount, &viewport);
		const auto viewportDescription = FormatViewport(viewport, viewportCount);

		ID3D11RenderTargetView* renderTargets[2]{};
		a_context->OMGetRenderTargets(2, renderTargets, nullptr);
		const auto rt0 = GetRenderTargetInfo(renderTargets[0]);
		const auto rt1 = GetRenderTargetInfo(renderTargets[1]);
		for (auto* renderTarget : renderTargets) {
			if (renderTarget) {
				renderTarget->Release();
			}
		}

		const auto rt0Description = FormatRenderTargetInfo(rt0);
		const auto rt1Description = FormatRenderTargetInfo(rt1);
		const auto details = std::string{ a_stateDetails };

		logger::info(
			"[LightLimitFix] Candidate PS state kind={} asmHash=0x{:08X} hash=0x{:08X} uid={} category={} target={} context=0x{:X} vtable=0x{:X} topology={} viewport={} rt0={} rt1={} state={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
			a_stateKind,
			metadata->asmHash,
			metadata->hash,
			metadata->uid,
			category,
			IsLightLimitFixPixelCandidate(*metadata),
			ToAddress(a_context),
			GetContextVTablePointer(a_context),
			static_cast<std::uint32_t>(topology),
			viewportDescription,
			rt0Description,
			rt1Description,
			details,
			FormatBufferSlots(*metadata),
			FormatTextureSlots(*metadata),
			FormatTextureDimensions(*metadata),
			metadata->instructionCount,
			metadata->sampleInstructionCount,
			FormatTextureSampleCounts(*metadata),
			metadata->hasDiscard,
			metadata->hasImmediateConstantBuffer,
			metadata->immediateConstantBufferRows);
	}

	void TraceLightLimitFixDrawContext(ID3D11DeviceContext* a_context, const char* a_drawKind, std::string_view a_drawCounts)
	{
		if (!a_context || !ShouldTraceLLFPixelCandidates(*ShaderCache::GetSingleton())) {
			return;
		}

		winrt::com_ptr<ID3D11PixelShader> pixelShader;
		a_context->PSGetShader(pixelShader.put(), nullptr, nullptr);
		if (!pixelShader) {
			return;
		}

		auto metadata = GetTrackedLightLimitFixPixelShader(pixelShader.get());
		bool surveyDraw = false;
		std::string surveyReason;
		if (!metadata) {
			metadata = GetObservedPixelShader(pixelShader.get());
			if (!metadata || IsLightLimitFixPixelTrackedCandidate(*metadata) || !ShouldSurveyBoundPixelShader(*metadata)) {
				return;
			}

			surveyDraw = true;
			surveyReason = ClassifyLightLimitFixSurveyRejection(*metadata);
		}

		D3D11_PRIMITIVE_TOPOLOGY topology{};
		a_context->IAGetPrimitiveTopology(&topology);

		D3D11_VIEWPORT viewport{};
		UINT viewportCount = 1;
		a_context->RSGetViewports(&viewportCount, &viewport);
		const auto viewportDescription = FormatViewport(viewport, viewportCount);

		ID3D11RenderTargetView* renderTargets[2]{};
		a_context->OMGetRenderTargets(2, renderTargets, nullptr);
		const auto rt0 = GetRenderTargetInfo(renderTargets[0]);
		const auto rt1 = GetRenderTargetInfo(renderTargets[1]);
		for (auto* renderTarget : renderTargets) {
			if (renderTarget) {
				renderTarget->Release();
			}
		}

		const auto category = ClassifyLightLimitFixPixelCandidate(*metadata);
		const auto rt0Description = FormatRenderTargetInfo(rt0);
		const auto rt1Description = FormatRenderTargetInfo(rt1);
		const auto key = std::format(
			"{:08X}:{}:{}:{}:{}:{}:{}:{}",
			metadata->asmHash,
			metadata->uid,
			category,
			a_drawKind,
			a_drawCounts,
			static_cast<std::uint32_t>(topology),
			viewportDescription,
			rt0Description + ":" + rt1Description);

		if (surveyDraw && IsLightLimitFixPixelImmediateConstantNearTarget(*metadata)) {
			bool shouldLogNearTargetDraw = false;
			bool shouldLogNearTargetDrawLimit = false;
			std::size_t nearTargetDrawUniqueCount = 0;
			{
				std::scoped_lock lock(llfCandidateLock);
				if (loggedLLFBoundPixelShaderNearTargetDraws.size() < kMaxLLFBoundPixelShaderNearTargetDrawLogs) {
					shouldLogNearTargetDraw = loggedLLFBoundPixelShaderNearTargetDraws.insert(key).second;
				} else if (!loggedLLFBoundPixelShaderNearTargetDrawLimit) {
					loggedLLFBoundPixelShaderNearTargetDrawLimit = true;
					shouldLogNearTargetDrawLimit = true;
				}
				nearTargetDrawUniqueCount = loggedLLFBoundPixelShaderNearTargetDraws.size();
			}

			if (shouldLogNearTargetDrawLimit) {
				logger::info(
					"[LightLimitFix] Bound PS near-target draw limit reached unique={} limit={}",
					nearTargetDrawUniqueCount,
					kMaxLLFBoundPixelShaderNearTargetDrawLogs);
			}

			if (shouldLogNearTargetDraw) {
				const auto boundConstantBuffers = FormatCurrentPixelShaderConstantBuffers(a_context, *metadata);
				const auto boundShaderResources = FormatCurrentPixelShaderResourceViews(a_context, *metadata);
				logger::info(
					"[LightLimitFix] Bound PS near-target draw asmHash=0x{:08X} hash=0x{:08X} uid={} reason={} shape=\"{}\" draw={} counts={} topology={} viewport={} rt0={} rt1={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} boundCBs={} boundSRVs={}",
					metadata->asmHash,
					metadata->hash,
					metadata->uid,
					surveyReason,
					FormatLightLimitFixPixelShape(*metadata),
					a_drawKind,
					a_drawCounts,
					static_cast<std::uint32_t>(topology),
					viewportDescription,
					rt0Description,
					rt1Description,
					FormatBufferSlots(*metadata),
					FormatTextureSlots(*metadata),
					FormatTextureDimensions(*metadata),
					metadata->instructionCount,
					metadata->sampleInstructionCount,
					FormatTextureSampleCounts(*metadata),
					boundConstantBuffers,
					boundShaderResources);
			}
		}

		if (surveyDraw) {
			bool shouldLogSurveyDraw = false;
			bool shouldLogSurveyDrawLimit = false;
			std::size_t surveyDrawUniqueCount = 0;
			{
				std::scoped_lock lock(llfCandidateLock);
				if (loggedLLFBoundPixelShaderSurveyDraws.size() < kMaxLLFBoundPixelShaderSurveyDrawLogs) {
					shouldLogSurveyDraw = loggedLLFBoundPixelShaderSurveyDraws.insert(key).second;
				} else if (!loggedLLFBoundPixelShaderSurveyDrawLimit) {
					loggedLLFBoundPixelShaderSurveyDrawLimit = true;
					shouldLogSurveyDrawLimit = true;
				}
				surveyDrawUniqueCount = loggedLLFBoundPixelShaderSurveyDraws.size();
			}

			if (shouldLogSurveyDrawLimit) {
				logger::info(
					"[LightLimitFix] Bound PS survey draw limit reached unique={} limit={}",
					surveyDrawUniqueCount,
					kMaxLLFBoundPixelShaderSurveyDrawLogs);
			}

			if (!shouldLogSurveyDraw) {
				return;
			}

			logger::info(
				"[LightLimitFix] Bound PS survey draw asmHash=0x{:08X} hash=0x{:08X} uid={} candidate=false reason={} shape=\"{}\" draw={} counts={} topology={} viewport={} rt0={} rt1={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
				metadata->asmHash,
				metadata->hash,
				metadata->uid,
				surveyReason,
				FormatLightLimitFixPixelShape(*metadata),
				a_drawKind,
				a_drawCounts,
				static_cast<std::uint32_t>(topology),
				viewportDescription,
				rt0Description,
				rt1Description,
				FormatBufferSlots(*metadata),
				FormatTextureSlots(*metadata),
				FormatTextureDimensions(*metadata),
				metadata->instructionCount,
				metadata->sampleInstructionCount,
				FormatTextureSampleCounts(*metadata),
				metadata->hasDiscard,
				metadata->hasImmediateConstantBuffer,
				metadata->immediateConstantBufferRows);
			return;
		}

		{
			std::scoped_lock lock(llfCandidateLock);
			if (!loggedLLFDrawContexts.insert(key).second) {
				return;
			}
		}

		const auto boundConstantBuffers = FormatCurrentPixelShaderConstantBuffers(a_context, *metadata);
		const auto boundShaderResources = FormatCurrentPixelShaderResourceViews(a_context, *metadata);
		logger::info(
			"[LightLimitFix] Candidate PS draw asmHash=0x{:08X} hash=0x{:08X} uid={} category={} target={} draw={} counts={} topology={} viewport={} rt0={} rt1={} buffers={} textures={} textureDims={} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={} boundCBs={} boundSRVs={}",
			metadata->asmHash,
			metadata->hash,
			metadata->uid,
			category,
			IsLightLimitFixPixelCandidate(*metadata),
			a_drawKind,
			a_drawCounts,
			static_cast<std::uint32_t>(topology),
			viewportDescription,
			rt0Description,
			rt1Description,
			FormatBufferSlots(*metadata),
			FormatTextureSlots(*metadata),
			FormatTextureDimensions(*metadata),
			metadata->instructionCount,
			metadata->sampleInstructionCount,
			FormatTextureSampleCounts(*metadata),
			metadata->hasDiscard,
			metadata->hasImmediateConstantBuffer,
			metadata->immediateConstantBufferRows,
			boundConstantBuffers,
			boundShaderResources);
	}

	void TraceLightLimitFixContextDiagnostics(const char* a_source, const char* a_phase, ID3D11DeviceContext* a_context, const void* a_rendererData, const void* a_rendererDevice)
	{
		if (!a_context) {
			return;
		}

		const auto contextAddress = ToAddress(a_context);
		const auto vtable = GetContextVTablePointer(a_context);
		const auto functions = FormatDrawVTableFunctions(vtable);
		const bool sameAsImmediate = observedImmediateContext && a_context == observedImmediateContext;
		const auto key = std::format("{}:{}:{:X}:{:X}:{}", a_source, a_phase, contextAddress, vtable, functions);
		{
			std::scoped_lock lock(llfCandidateLock);
			if (!loggedLLFContextDiagnostics.insert(key).second) {
				return;
			}
		}

		logger::info(
			"[LightLimitFix] PreNG context diagnostics source={} phase={} context=0x{:X} vtable=0x{:X} immediateContext=0x{:X} sameAsImmediate={} rendererData=0x{:X} rendererDevice=0x{:X} funcs={}",
			a_source,
			a_phase,
			contextAddress,
			vtable,
			ToAddress(observedImmediateContext),
			sameAsImmediate,
			ToAddress(a_rendererData),
			ToAddress(a_rendererDevice),
			functions);
	}

	bool ShouldEnableLightLimitFixPixelCandidateDiagnostics()
	{
		return ShouldTraceLLFPixelCandidateDiagnostics() || ShouldTrackPreNGDFLightDrawTargets();
	}

	bool ShouldTraceLLFPixelCandidates(const ShaderCache& a_cache)
	{
		(void)a_cache;
		return ShouldTraceLLFPixelCandidateDiagnostics();
	}

	bool IsLightLimitFixPixelTrackedCandidate(const ShaderCache::ShaderMetadata& a_metadata)
	{
		return IsLightLimitFixPixelCandidate(a_metadata) ||
		       IsLightLimitFixPixelImmediateConstantNearTarget(a_metadata);
	}

	void TrackLightLimitFixPixelShader(ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata)
	{
		if (!a_pixelShader) {
			return;
		}

		std::scoped_lock lock(llfCandidateLock);
		llfCandidatePixelShaders[a_pixelShader] = a_metadata;
	}

	void TrackObservedPixelShader(ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata)
	{
		if (!a_pixelShader) {
			return;
		}

		std::scoped_lock lock(llfCandidateLock);
		observedPixelShaderMetadata[a_pixelShader] = a_metadata;
	}

	void TraceLightLimitFixPixelCandidate(ID3D11Device* a_device, ID3D11PixelShader* a_pixelShader, const ShaderCache::ShaderMetadata& a_metadata)
	{
		const auto key = std::format("{}:{:08X}", a_metadata.uid, a_metadata.hash);
		{
			std::scoped_lock lock(llfCandidateLock);
			if (!loggedLLFPixelCandidates.insert(key).second) {
				return;
			}
		}

		logger::info(
			"[LightLimitFix] Candidate PS asmHash=0x{:08X} hash=0x{:08X} uid={} category={} target={} device=0x{:X} shader=0x{:X} size={} buffers={} textures={} textureDims={} inputCount={} outputCount={} inputMask=0x{:X} outputMask=0x{:X} instructions={} samples={} textureSamples={} discard={} immediateCB={} immediateRows={}",
			a_metadata.asmHash,
			a_metadata.hash,
			a_metadata.uid,
			ClassifyLightLimitFixPixelCandidate(a_metadata),
			IsLightLimitFixPixelCandidate(a_metadata),
			ToAddress(a_device),
			ToAddress(a_pixelShader),
			a_metadata.size,
			FormatBufferSlots(a_metadata),
			FormatTextureSlots(a_metadata),
			FormatTextureDimensions(a_metadata),
			a_metadata.inputCount,
			a_metadata.outputCount,
			a_metadata.inputMask,
			a_metadata.outputMask,
			a_metadata.instructionCount,
			a_metadata.sampleInstructionCount,
			FormatTextureSampleCounts(a_metadata),
			a_metadata.hasDiscard,
			a_metadata.hasImmediateConstantBuffer,
			a_metadata.immediateConstantBufferRows);
	}

#endif
}

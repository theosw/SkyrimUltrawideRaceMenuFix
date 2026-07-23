#include <PCH.h>

// The vanilla RaceSexCamera (character creation / showracemenu) computes its
// zoom distances from the horizontal extent of the real projection. On
// displays wider than 16:9 the horizontal half-tangent grows linearly with
// aspect ratio, so the computed camera distance shrinks by the same factor:
// at 21:9 the camera sits ~30% too close, at 32:9 it halves and ends up
// inside the character's head. There is no INI/GMST knob for these distances.
//
// This plugin hooks RaceSexCamera::Update and pushes the camera straight back
// along its view axis so the on-axis distance to the character becomes what a
// 16:9 screen gets. Vertical FOV is aspect-independent in Skyrim (Hor+), so
// restoring the 16:9 distance restores the exact 16:9 framing, with the extra
// horizontal width as bonus breathing room.

namespace
{
	struct Settings
	{
		// <= 0 means automatic: real frustum aspect / (16:9).
		float distanceScale = 0.0f;
		// Taste multiplier on top; 1.0 = exact 16:9 framing.
		float userScale = 1.0f;
		bool  verboseLog = false;

		void Load()
		{
			CSimpleIniA ini;
			ini.SetUnicode();
			const auto path = IniPath();
			if (ini.LoadFile(path.c_str()) == SI_OK) {
				distanceScale = static_cast<float>(ini.GetDoubleValue("Settings", "fDistanceScale", distanceScale));
				userScale = static_cast<float>(ini.GetDoubleValue("Settings", "fUserScale", userScale));
				verboseLog = ini.GetBoolValue("Settings", "bVerboseLog", verboseLog);
			}
		}

	private:
		static std::string IniPath()
		{
			// Resolve next to the DLL so the MO2 virtual file system and odd
			// working directories can't misplace it.
			HMODULE module = nullptr;
			if (::GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&IniPath),
					&module) &&
				module) {
				std::wstring buffer(MAX_PATH, L'\0');
				const auto written = ::GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
				if (written != 0) {
					buffer.resize(written);
					auto path = std::filesystem::path(buffer).parent_path() / L"UltrawideChargenCameraFix.ini";
					return path.string();
				}
			}
			return "Data/SKSE/Plugins/UltrawideChargenCameraFix.ini";
		}
	};

	Settings g_settings;

	class ChargenCameraFix
	{
	public:
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_RaceSexCamera[0] };
			_origUpdate = vtbl.write_vfunc(0x2, Hook_Update);
			logger::info("RaceSexCamera::Update hooked");
		}

	private:
		static void Hook_Update(RE::TESCamera* a_this)
		{
			// The camera only updates while the menu is up, so a gap since the
			// last update means a fresh chargen session: re-read the INI so
			// fUserScale can be tuned without restarting the game.
			const auto now = std::chrono::steady_clock::now();
			if (now - s_lastUpdate > 2s) {
				g_settings.Load();
				s_haveOffsetCache = false;
			}
			s_lastUpdate = now;

			// Give the engine back its own (uncorrected) position before it
			// integrates this frame, so our correction never feeds back into
			// the engine's zoom smoothing. If the position is not the one we
			// wrote last frame, something else moved the camera (RaceMenu's
			// camera tab, another plugin): adopt it instead of fighting.
			auto* root = a_this->cameraRoot.get();
			if (root && s_haveShadow) {
				if (Nearly(root->local.translate, s_shownPos)) {
					root->local.translate = s_enginePos;
				} else {
					s_haveShadow = false;
				}
			}

			_origUpdate(a_this);

			root = a_this->cameraRoot.get();
			if (!root) {
				s_haveShadow = false;
				return;
			}

			const RE::NiPoint3 enginePos = root->local.translate;

			// The engine only moves this camera on zoom changes and menu
			// transitions; while it holds still, hold our correction still
			// too, so idle-animation sway of the head bone cannot wiggle the
			// amplified camera.
			RE::NiPoint3 fixedPos;
			if (s_haveOffsetCache && Nearly(enginePos, s_cachedEnginePos)) {
				fixedPos = s_cachedFixedPos;
			} else {
				const float scale = DistanceScale(root);
				RE::NiPoint3 head;
				if (std::abs(scale - 1.0f) < 0.01f || !HeadPosition(head)) {
					s_haveShadow = false;
					s_haveOffsetCache = false;
					return;
				}

				const RE::NiPoint3 toHead = head - enginePos;
				const float distance = toHead.Length();
				if (distance < 1.0f) {
					s_haveShadow = false;
					s_haveOffsetCache = false;
					return;
				}

				// The camera looks at (roughly) the head. Find which rotated
				// local axis is the view direction instead of assuming a
				// convention.
				const RE::NiPoint3 axes[3] = {
					root->local.rotate * RE::NiPoint3{ 1.0f, 0.0f, 0.0f },
					root->local.rotate * RE::NiPoint3{ 0.0f, 1.0f, 0.0f },
					root->local.rotate * RE::NiPoint3{ 0.0f, 0.0f, 1.0f },
				};
				RE::NiPoint3 forward = axes[1];
				float best = 0.0f;
				for (const auto& axis : axes) {
					const float alignment = axis.Dot(toHead) / distance;
					if (std::abs(alignment) > std::abs(best)) {
						best = alignment;
						forward = axis;
					}
				}
				if (best < 0.0f) {
					forward = -forward;
				}

				const float depth = forward.Dot(toHead);
				if (depth < 1.0f) {
					s_haveShadow = false;
					s_haveOffsetCache = false;
					return;
				}

				// Push straight back along the view axis: on-axis distance
				// becomes scale * depth, lateral placement stays untouched.
				fixedPos = enginePos - forward * ((scale - 1.0f) * depth);
				s_cachedEnginePos = enginePos;
				s_cachedFixedPos = fixedPos;
				s_haveOffsetCache = true;

				if (g_settings.verboseLog && (s_frame++ % 60) == 0) {
					logger::info(
						"scale={:.3f} depth={:.1f} head=({:.1f} {:.1f} {:.1f}) engine=({:.1f} {:.1f} {:.1f}) fixed=({:.1f} {:.1f} {:.1f})",
						scale, depth, head.x, head.y, head.z,
						enginePos.x, enginePos.y, enginePos.z,
						fixedPos.x, fixedPos.y, fixedPos.z);
				}
			}

			root->local.translate = fixedPos;
			RE::NiUpdateData updateData{};
			root->Update(updateData);

			s_enginePos = enginePos;
			s_shownPos = fixedPos;
			s_haveShadow = true;
		}

		static float DistanceScale(RE::NiNode* a_root)
		{
			float scale = g_settings.distanceScale;
			if (scale <= 0.0f) {
				const float aspect = FrustumAspect(a_root);
				if (aspect <= 0.0f) {
					return 1.0f;
				}
				scale = aspect / (16.0f / 9.0f);
			}
			scale *= g_settings.userScale;
			return std::clamp(scale, 0.3f, 4.0f);
		}

		// Aspect ratio straight from the NiCamera frustum: the ground truth
		// the projection uses, immune to resolution lies told elsewhere.
		static float FrustumAspect(RE::NiNode* a_root)
		{
			for (const auto& child : a_root->GetChildren()) {
				if (const auto* camera = netimmerse_cast<RE::NiCamera*>(child.get())) {
					const auto& frustum = camera->GetRuntimeData2().viewFrustum;
					const float width = frustum.fRight - frustum.fLeft;
					const float height = frustum.fTop - frustum.fBottom;
					if (height > 1.0e-6f) {
						return width / height;
					}
				}
			}
			return 0.0f;
		}

		static bool HeadPosition(RE::NiPoint3& a_out)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return false;
			}
			auto* model = player->Get3D(false);
			if (!model) {
				model = player->Get3D(true);
			}
			if (!model) {
				return false;
			}
			static const RE::BSFixedString headName("NPC Head [Head]");
			if (const auto* headNode = model->GetObjectByName(headName)) {
				a_out = headNode->world.translate;
				return true;
			}
			a_out = player->GetPosition();
			a_out.z += 120.0f;  // rough eye height
			return true;
		}

		static bool Nearly(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs)
		{
			return std::abs(a_lhs.x - a_rhs.x) < 0.01f &&
			       std::abs(a_lhs.y - a_rhs.y) < 0.01f &&
			       std::abs(a_lhs.z - a_rhs.z) < 0.01f;
		}

		static inline REL::Relocation<decltype(Hook_Update)> _origUpdate;
		static inline bool s_haveShadow = false;
		static inline RE::NiPoint3 s_enginePos{};
		static inline RE::NiPoint3 s_shownPos{};
		static inline bool s_haveOffsetCache = false;
		static inline RE::NiPoint3 s_cachedEnginePos{};
		static inline RE::NiPoint3 s_cachedFixedPos{};
		static inline std::uint32_t s_frame = 0;
		static inline std::chrono::steady_clock::time_point s_lastUpdate{};
	};

	void InitializeLog()
	{
		std::vector<spdlog::sink_ptr> sinks;
#ifndef NDEBUG
		sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif
		auto path = logger::log_directory();
		if (!path) {
			util::report_and_fail("Failed to find standard logging directory"sv);
		}
		*path /= std::format("{}.log"sv, Plugin::NAME);
		sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));

#ifndef NDEBUG
		const auto level = spdlog::level::trace;
#else
		const auto level = spdlog::level::info;
#endif

		auto log = std::make_shared<spdlog::logger>("global log"s, sinks.begin(), sinks.end());
		log->set_level(level);
		log->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v"s);
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	logger::info("{} v{} loading", Plugin::NAME, Plugin::VERSION_STRING);

	SKSE::Init(a_skse);

	g_settings.Load();
	ChargenCameraFix::Install();

	logger::info("{} loaded", Plugin::NAME);
	return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName(Plugin::NAME.data());
	v.PluginVersion(Plugin::VERSION);
	v.UsesAddressLibrary(true);
	v.HasNoStructUse();
	return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->name = SKSEPlugin_Version.pluginName;
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->version = SKSEPlugin_Version.pluginVersion;
	return true;
}

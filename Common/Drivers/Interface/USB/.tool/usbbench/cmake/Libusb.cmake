# libusb-1.0 for usbbench. Sets USBBENCH_LIBUSB_TARGET to the target usbbench links against.
#
# Windows: the official libusb release, MinGW64 static library and header. The archive is downloaded once into
# third_party/ (git-ignored) and checked against its SHA-256 before and after the download. CMake cannot unpack it: its
# libarchive fails on the archive's ARM64 filter and keeps only part of the files, so the two files usbbench needs are
# unpacked with 7-Zip and checked against their own SHA-256 values. A stamp file records a finished extraction.
# Other platforms: the system libusb-1.0, found through pkg-config.

if(WIN32)
	set(USBBENCH_LIBUSB_VERSION "1.0.30")
	set(USBBENCH_LIBUSB_ARCHIVE_SHA256 "7fb1dfec805b97983763d7d0ae244320da12add1003d4249c96cc4d586398c79")
	set(USBBENCH_LIBUSB_LIBRARY_SHA256 "195856a13a0109a71437fa5c505877577115fded1db79f24a3160074133c14ff")
	set(USBBENCH_LIBUSB_HEADER_SHA256 "a61260ab145b051b86df2b0575956f01810190abe2c7df6aca831c33bdc8082c")
	set(USBBENCH_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
	set(USBBENCH_LIBUSB_ARCHIVE "${USBBENCH_THIRD_PARTY}/libusb-${USBBENCH_LIBUSB_VERSION}.7z")
	set(USBBENCH_LIBUSB_ROOT "${USBBENCH_THIRD_PARTY}/libusb-${USBBENCH_LIBUSB_VERSION}")
	set(USBBENCH_LIBUSB_LIBRARY "${USBBENCH_LIBUSB_ROOT}/MinGW64/static/libusb-1.0.a")
	set(USBBENCH_LIBUSB_HEADER "${USBBENCH_LIBUSB_ROOT}/include/libusb.h")
	set(USBBENCH_LIBUSB_STAMP "${USBBENCH_LIBUSB_ROOT}/extracted.stamp")

	if(NOT EXISTS "${USBBENCH_LIBUSB_STAMP}" OR NOT EXISTS "${USBBENCH_LIBUSB_LIBRARY}")
		file(MAKE_DIRECTORY "${USBBENCH_THIRD_PARTY}")

		# An archive that does not match the pinned hash is left over from an earlier run: drop it and fetch it again.
		if(EXISTS "${USBBENCH_LIBUSB_ARCHIVE}")
			file(SHA256 "${USBBENCH_LIBUSB_ARCHIVE}" usbbenchArchiveSha256)
			if(NOT usbbenchArchiveSha256 STREQUAL USBBENCH_LIBUSB_ARCHIVE_SHA256)
				file(REMOVE "${USBBENCH_LIBUSB_ARCHIVE}")
			endif()
		endif()

		if(NOT EXISTS "${USBBENCH_LIBUSB_ARCHIVE}")
			# The download goes to a partial name, so an interrupted or rejected download never looks like the archive.
			# The hash is checked here and not with file(DOWNLOAD EXPECTED_HASH): that option stops configure on a
			# mismatch and leaves the partial file behind.
			set(usbbenchArchivePart "${USBBENCH_LIBUSB_ARCHIVE}.part")
			file(REMOVE "${usbbenchArchivePart}")
			file(DOWNLOAD
				"https://github.com/libusb/libusb/releases/download/v${USBBENCH_LIBUSB_VERSION}/libusb-${USBBENCH_LIBUSB_VERSION}.7z"
				"${usbbenchArchivePart}"
				TLS_VERIFY ON
				STATUS usbbenchDownloadStatus
			)
			list(GET usbbenchDownloadStatus 0 usbbenchDownloadCode)
			if(NOT usbbenchDownloadCode EQUAL 0)
				list(GET usbbenchDownloadStatus 1 usbbenchDownloadMessage)
				file(REMOVE "${usbbenchArchivePart}")
				message(FATAL_ERROR "usbbench: downloading libusb ${USBBENCH_LIBUSB_VERSION} failed: ${usbbenchDownloadMessage}")
			endif()

			file(SHA256 "${usbbenchArchivePart}" usbbenchPartSha256)
			if(NOT usbbenchPartSha256 STREQUAL USBBENCH_LIBUSB_ARCHIVE_SHA256)
				file(REMOVE "${usbbenchArchivePart}")
				message(FATAL_ERROR "usbbench: the downloaded libusb archive has SHA-256 ${usbbenchPartSha256}, expected ${USBBENCH_LIBUSB_ARCHIVE_SHA256}; the download was discarded")
			endif()
			file(RENAME "${usbbenchArchivePart}" "${USBBENCH_LIBUSB_ARCHIVE}")
		endif()

		# Checked again before extraction, whatever the download reported.
		file(SHA256 "${USBBENCH_LIBUSB_ARCHIVE}" usbbenchArchiveSha256)
		if(NOT usbbenchArchiveSha256 STREQUAL USBBENCH_LIBUSB_ARCHIVE_SHA256)
			file(REMOVE "${USBBENCH_LIBUSB_ARCHIVE}")
			message(FATAL_ERROR "usbbench: the libusb archive has an unexpected SHA-256 and was removed; configure again to download it")
		endif()

		find_program(USBBENCH_SEVENZIP NAMES 7z 7zz 7za)
		if(NOT USBBENCH_SEVENZIP)
			message(FATAL_ERROR "usbbench: 7-Zip is needed to unpack libusb (for example: scoop install 7zip)")
		endif()

		execute_process(
			COMMAND "${USBBENCH_SEVENZIP}" x "${USBBENCH_LIBUSB_ARCHIVE}" "-o${USBBENCH_LIBUSB_ROOT}"
				"MinGW64/static/libusb-1.0.a" "include/libusb.h" -y
			RESULT_VARIABLE usbbenchExtractCode
			OUTPUT_QUIET
			ERROR_VARIABLE usbbenchExtractError
		)
		if(NOT usbbenchExtractCode EQUAL 0)
			message(FATAL_ERROR "usbbench: unpacking libusb failed (${usbbenchExtractCode}): ${usbbenchExtractError}")
		endif()

		if(NOT EXISTS "${USBBENCH_LIBUSB_LIBRARY}" OR NOT EXISTS "${USBBENCH_LIBUSB_HEADER}")
			message(FATAL_ERROR "usbbench: the libusb archive does not contain MinGW64/static/libusb-1.0.a and include/libusb.h")
		endif()

		file(SHA256 "${USBBENCH_LIBUSB_LIBRARY}" usbbenchLibrarySha256)
		file(SHA256 "${USBBENCH_LIBUSB_HEADER}" usbbenchHeaderSha256)
		if(NOT usbbenchLibrarySha256 STREQUAL USBBENCH_LIBUSB_LIBRARY_SHA256)
			message(FATAL_ERROR "usbbench: libusb-1.0.a has an unexpected SHA-256 after unpacking: ${usbbenchLibrarySha256}")
		endif()
		if(NOT usbbenchHeaderSha256 STREQUAL USBBENCH_LIBUSB_HEADER_SHA256)
			message(FATAL_ERROR "usbbench: libusb.h has an unexpected SHA-256 after unpacking: ${usbbenchHeaderSha256}")
		endif()

		file(WRITE "${USBBENCH_LIBUSB_STAMP}" "libusb ${USBBENCH_LIBUSB_VERSION}\n")
	endif()

	add_library(usbbenchLibusb STATIC IMPORTED)
	set_target_properties(usbbenchLibusb PROPERTIES
		IMPORTED_LOCATION "${USBBENCH_LIBUSB_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${USBBENCH_LIBUSB_ROOT}/include"
	)
	set(USBBENCH_LIBUSB_TARGET usbbenchLibusb)
else()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(USBBENCH_LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
	set(USBBENCH_LIBUSB_TARGET PkgConfig::USBBENCH_LIBUSB)
endif()

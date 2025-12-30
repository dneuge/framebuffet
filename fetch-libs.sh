#!/bin/bash

# FETCHLIBS_DOWNLOAD_ONLY=1 can be set to only download missing dependencies
# FETCHLIBS_UPDATE_MIRROR=0 can be set to skip updates of existing Git mirror bundles

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
cd "${script_dir}"

mkdir -p lib/_downloads
cd lib

function die {
    echo $@
    exit 1
}

function download {
    description="$1"
    extract_dir="$2"
    filename="$3"
    expected_size="$4"
    url="$5"
    expected_shasum="$6"
    expected_mdsum="$7"

    if [[ -e "${extract_dir}" ]]; then
        echo "-- ${description} is already unpacked"
        return
    fi
    
    echo "-- ${description} is missing"

    if [[ -f "_downloads/${filename}" ]]; then
        echo "- using cached ${description} archive"
    else
        echo "- fetching ${description} from ${url}"

        [[ -e download.tmp ]] && (rm download.tmp || die "deleting temporary file download.tmp failed")
        curl "${url}" -L -o download.tmp || die "download failed"
        
        actual_size="$(wc -c download.tmp | sed -e 's#^[[:space:]]*##g' | cut -d' ' -f1)"
        actual_shasum="$(sha256sum download.tmp | cut -d' ' -f1)"
        actual_mdsum="$(md5sum download.tmp | cut -d' ' -f1)"
        
        if [[ "${actual_size}" != "${expected_size}" || "${actual_shasum}" != "${expected_shasum}" || "${actual_mdsum}" != "${expected_mdsum}" ]]; then
            echo "Download verification failed, check download.tmp:"
            echo " expected size ${expected_size}, got ${actual_size}"
            echo " expected SHA256 ${expected_shasum}, got ${actual_shasum}"
            echo " expected MD5 ${expected_mdsum}, got ${actual_mdsum}"
            exit 1
        fi
        
        mv download.tmp "_downloads/${filename}" || die "Failed to move downloaded file to cache"
    fi
    
    if [[ "${FETCHLIBS_DOWNLOAD_ONLY:-0}" == "1" ]]; then
        echo "- download only has been requested, skipping extraction..."
        return
    fi
    
    echo "- extracting ${description} from ${filename} to ${extract_dir}"
    mkdir "${extract_dir}" || die "Failed to create extraction directory ${extract_dir}"
    
    if [[ "${filename}" =~ \.zip$ ]]; then
        unzip -d "${extract_dir}" "_downloads/${filename}" || die "Failed to unpack ${filename} to ${extract_dir}"
    elif [[ "${filename}" =~ \.7z$ ]]; then
        7z x -o"${extract_dir}" "_downloads/${filename}" || die "Failed to unpack ${filename} to ${extract_dir}"
    elif [[ "${filename}" =~ \.tar\.gz$ ]]; then
        previous_dir="$(pwd)"
        cd "${extract_dir}"
        tar -x -z -f "${previous_dir}/_downloads/${filename}" || die "Failed to unpack ${filename} to ${extract_dir}"
        cd "${previous_dir}"
    elif [[ "${filename}" =~ \.tar\.xz$ ]]; then
        previous_dir="$(pwd)"
        cd "${extract_dir}"
        tar -x -J -f "${previous_dir}/_downloads/${filename}" || die "Failed to unpack ${filename} to ${extract_dir}"
        cd "${previous_dir}"
    else
        rmdir "${extract_dir}"
        die "Unhandled file suffix: ${filename}"
    fi
    
    # if there was a single directory on root level, pull it up
    subs="$(find "${extract_dir}" -mindepth 1 -maxdepth 1)"
    if [[ "$(nl <<<"$subs" | tail -n1 | sed -r -e 's/[[:space:]]*([0-9]+)[[:space:]].*/\1/')" == "1" && -d "${subs}" ]]; then
        [[ ! -e "_tmp" ]] || die "unable to pull up directory, _tmp exists already"
        mv "${subs}" _tmp || die "failed to pull directory ${subs} up (1)"
        rmdir "${extract_dir}" || die "failed to pull directory ${subs} up (2)"
        mv _tmp "${extract_dir}" || die "failed to pull directory ${subs} up (3)"
    fi
    
    echo "- ${description} has been unpacked"
    
    echo "- applying patches"
    if ! ls ${extract_dir}-*.patch >/dev/null 2>&1; then
        echo "no patches found"
    else
        for patch_file in ${extract_dir}-*.patch; do
            patch_binary=0
            if grep -F "${patch_file}" "${script_dir}/lib/_framebuffet_patch_options.conf" | grep -E '\s+--binary' >/dev/null 2>&1; then
                patch_binary=1
            fi
            
            if [[ $patch_binary -ne 0 ]]; then
                echo "applying ${patch_file} (binary mode)"
                patch -p1 --binary <"${patch_file}" || die "patch failed"
            else
                echo "applying ${patch_file}"
                patch -p1 <"${patch_file}" || die "patch failed"
            fi
        done
    fi
}

function git_mirror {
    description="$1"
    filename="$2"
    url="$3"
    branch="$4"

    echo "-- mirroring Git repository for ${description}"

    bundle_file="${script_dir}/lib/_downloads/${filename}"
    mirror_workdir="${script_dir}/lib/_downloads/.git_mirror_work"

    if [[ -e "${bundle_file}" ]] && [[ "${FETCHLIBS_UPDATE_MIRROR:-1}" -eq 0 ]]; then
        echo "- bundle already exists and updates have been disabled, skipping..."
        return
    fi

    if [[ -d "${mirror_workdir}" ]]; then
        rm -Rf "${mirror_workdir}" || die "failed to delete git mirror working directory ${mirror_workdir}"
    fi

    mkdir -p "${mirror_workdir}" || die "failed to create git mirror working directory ${mirror_workdir}"
    cd "${mirror_workdir}" || die "failed to change into git mirror working directory ${mirror_workdir}"

    if [[ -e "${bundle_file}" ]]; then
        echo "- restoring previous state from bundle ${bundle_file}"
        git clone --branch ${branch} "${bundle_file}" . || die "git clone from ${bundle_file} failed for branch ${branch}"
    else
        echo "- initializing repository with branch ${branch}"
        git init . || die "git init failed"
        git checkout -b ${branch} || die "branch ${branch} could not be created"
    fi

    echo "- updating from ${url}"
    git pull --tags "${url}" ${branch} || die "pulling ${branch} from ${url} failed"

    echo "- archiving to ${bundle_file}"
    git bundle create "${bundle_file}" --branches --tags || die "archival to ${bundle_file} failed"

    rm -Rf "${mirror_workdir}" || die "failed to delete git mirror working directory ${mirror_workdir}"
    cd "${script_dir}/lib"
}

function git_unbundle {
    description="$1"
    extract_dir="$2"
    filename="$3"
    ref="$4"

    cd "${script_dir}/lib"

    if [[ -e "${extract_dir}" ]]; then
        echo "-- ${description} is already unpacked"
        return
    fi

    echo "-- ${description} is missing"

    if [[ "${FETCHLIBS_DOWNLOAD_ONLY:-0}" == "1" ]]; then
        echo "- download only has been requested, skipping extraction..."
        return
    fi

    if [[ ! -f "_downloads/${filename}" ]]; then
        echo "Bundle is missing, Git needs to be mirrored first!"
        exit 1
    fi

    echo "- unbundling ${description} into ${extract_dir}"
    mkdir "${extract_dir}" || die "Directory ${extract_dir} already exists."

    cd "${extract_dir}" || die "Failed to change into ${extract_dir}"
    git init . || die "Failed to initialize Git repository."
    git config --local advice.detachedHead false
    git bundle unbundle "${script_dir}/lib/_downloads/${filename}" || die "git failed to unbundle"

    echo "- switching to ${ref}"
    git checkout "${ref}" || die "git failed to checkout"

    cd "${script_dir}/lib"

    echo "- applying patches"
    if ! ls ${extract_dir}-*.patch >/dev/null 2>&1; then
        echo "no patches found"
    else
        for patch_file in ${extract_dir}-*.patch; do
            patch_binary=0
            if grep -F "${patch_file}" "${script_dir}/lib/_framebuffet_patch_options.conf" | grep -E '\s+--binary' >/dev/null 2>&1; then
                patch_binary=1
            fi
            
            if [[ $patch_binary -ne 0 ]]; then
                echo "applying ${patch_file} (binary mode)"
                patch -p1 --binary <"${patch_file}" || die "patch failed"
            else
                echo "applying ${patch_file}"
                patch -p1 <"${patch_file}" || die "patch failed"
            fi
        done
    fi
}

download "zlib" zlib zlib-1.3.1.zip 1655249 https://github.com/madler/zlib/releases/download/v1.3.1/zlib131.zip 72af66d44fcc14c22013b46b814d5d2514673dda3d115e64b690c1ad636e7b17 ef76f7ebfd97778a6653b1d8413541c0
download "lzo" lzo lzo-2.10.tar.gz 600622 https://www.oberhumer.com/opensource/lzo/download/lzo-2.10.tar.gz c0f892943208266f9b6543b3ae308fab6284c5c90e627931446fb49b4221a072 39d3f3f9c55c87b1e5d6888e1420f4b5
download "libjpeg-turbo" libjpeg_turbo libjpeg-turbo-2.1.4.zip 2667456 https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/2.1.4.zip 658647569c8e7a5728c6692fd93df65d17e491f5808522f93fa908e339337cab ef4c3147b67629958a4c4a7b5af898eb
download "libjpeg" libjpeg libjpeg-9f.zip 1209165 https://www.ijg.org/files/jpegsr9f.zip 6255da8c89e09d694e6800688c76145eb6870a76ac0d36c74fccd61b3940aafa 8bd2706c80ac696856a1334430a6ffd1
download "LibVNCServer (LibVNCClient)" VNC LibVNCServer-0.9.14.zip 699430 https://github.com/LibVNC/libvncserver/archive/refs/tags/LibVNCServer-0.9.14.zip 43203e89483cbbb43df2c5939412a3f4d7ade0daf9f80005010150d665c83290 d240f0f2dfd4d632b93ddeb66885a007
download "Mesa" mesa mesa-22.3.1.tar.xz 16972884 https://archive.mesa3d.org/older-versions/22.x/mesa-22.3.1.tar.xz 3c9cd611c0859d307aba0659833386abdca4c86162d3c275ba5be62d16cf31eb 5644bca27c9be0092c98f1ec0e1bedd5
download "cJSON" cjson cjson-1.7.15.zip 458334 https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.15.zip c55519316d940757ef93a779f1db1ca809dbf979c551861f339d35aaea1c907c c7100b3764d8718284d0d031d10e17d9
download "xcb-proto" xcb_proto xcb-proto-1.17.0.tar.xz 151748 https://xorg.freedesktop.org/archive/individual/proto/xcb-proto-1.17.0.tar.xz 2c1bacd2110f4799f74de6ebb714b94cf6f80fb112316b1219480fd22562148c c415553d2ee1a8cea43c3234a079b53f
download "xcb" xcb libxcb-1.17.0.tar.xz 455208 https://xorg.freedesktop.org/archive/individual/lib/libxcb-1.17.0.tar.xz 599ebf9996710fea71622e6e184f3a8ad5b43d0e5fa8c4e407123c88a59a6d55 96565523e9f9b701fcb35d31f1d4086e
git_mirror "xcb-util" freedesktop.libxcb-util.master.bundle https://gitlab.freedesktop.org/xorg/lib/libxcb-util.git master
git_unbundle "xcb-util" xcb_util freedesktop.libxcb-util.master.bundle bffd8df1725c0fae9105a097e75b466e2f49d7bd # tag: xcb-util-0.4.1-gitlab
git_mirror "xcb-util-image" freedesktop.libxcb-util-image.master.bundle https://gitlab.freedesktop.org/xorg/lib/libxcb-image.git master
git_unbundle "xcb-util-image" xcb_util_image freedesktop.libxcb-util-image.master.bundle 1abfc17b1e08e46dbdf34b4ef36097a8c307a8ec # tag: xcb-util-image-0.4.1-gitlab
git_mirror "xcb-util-m4" freedesktop.libxcb-util-m4.master.bundle https://gitlab.freedesktop.org/xorg/util/xcb-util-m4.git master
#git_unbundle "xcb-util-m4" xcb_util_m4 freedesktop.libxcb-util-m4.master.bundle c617eee22ae5c285e79e81ec39ce96862fd3262f # as used by xcb-util and xcb-util-image

#!/usr/bin/env bash
# Shared application/fixture selection. SDK production remains in kobuild.
# Each target pins the stock firmware used as its userspace fixture, the
# release string in that firmware's /etc/version.txt, and its stock glibc
# loader and libc files with their digests. Applications keep the glibc 2.20
# symbol ceiling on every target; newer firmware glibc remains compatible.
kindle_target() {
    target=${1:-pw5}
    case "$target" in
        pw5)
            preset=kindle; output=kindle; fixture=kindle-sdk
            sdk_image=localhost/kobuild:kindle
            abi=hard-float; interpreter=/lib/ld-linux-armhf.so.3; qemu_cpu=cortex-a7
            firmware=update_kindle_all_new_paperwhite_11th_5.19.2.bin
            firmware_sha=64ddcbae52f0009b5d3f9974d93610db7f26a8787c895c55f413127af474b987
            firmware_size=361949776
            firmware_release=040-juno_1902_malbec_bellatrix-467076
            runtime_glibc=2.20; runtime_loader=lib/ld-2.20.so; runtime_libc=lib/libc-2.20.so
            loader_sha=8b36a28d91b6fdb20849869b13befeb0cf1024001c1a4e439189bd73dddf8b4b
            libc_sha=8357e99917260277a1c137383a4edbdc5bcb4318464cb1f5a2096533aff3b2d9
            ;;
        pw3)
            preset=kindle-pw3; output=kindle-pw3; fixture=kindle-pw3-sdk
            sdk_image=localhost/kobuild:kindle-pw3
            abi=soft-float; interpreter=/lib/ld-linux.so.3; qemu_cpu=cortex-a9
            firmware=update_kindle_all_new_paperwhite_5.13.5.bin
            firmware_sha=943796120a94f96743f9c34d59b9727a21c91a15e61ef91f9b23159c6d045775
            firmware_size=253013186
            firmware_release=030-juno_1305_muscat_wario-371675
            runtime_glibc=2.20; runtime_loader=lib/ld-2.20.so; runtime_libc=lib/libc-2.20.so
            loader_sha=117de64bdc41e8371b420dae74a821aa3d9e9d02dfcc3f27c76cb948073cece1
            libc_sha=1338645be2267bff9061746d1a477cc6e036b272d230c199725e392d9300748d
            ;;
        colorsoft)
            # Same hard-float kindlehf SDK and loader as PW5; separate output,
            # package and Colorsoft firmware fixture. See doc/colorsoft.md.
            preset=kindle-colorsoft; output=kindle-colorsoft; fixture=kindle-colorsoft-sdk
            sdk_image=localhost/kobuild:kindle
            abi=hard-float; interpreter=/lib/ld-linux-armhf.so.3; qemu_cpu=cortex-a7
            firmware=update_kindle_colorsoft_5.20.1.bin
            firmware_sha=6d2fc159937b244d49cc9dd0bcc5f5551b7ff103a02a9fc74b1b6628f2b61aa9
            firmware_size=495503556
            firmware_release=050-juno_2001_seabreeze_bellatrix4-489385
            runtime_glibc=2.35; runtime_loader=lib/ld-linux-armhf.so.3; runtime_libc=lib/libc.so.6
            loader_sha=a089ca56fba8e33fb8d87b791ac9f81c9065d0ae9d9d0ffb899037f7ed284701
            libc_sha=5a34d04c0392bf6b69b361ffab68b8c2a064b444a4c6c58891a3607bd4593431
            ;;
        *) echo "Unknown Kindle target: $target (choose pw5, pw3 or colorsoft)" >&2; return 2 ;;
    esac
}

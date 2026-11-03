#!/usr/bin/env bash
# Shared application/fixture selection. SDK production remains in kobuild.
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
            ;;
        pw3)
            preset=kindle-pw3; output=kindle-pw3; fixture=kindle-pw3-sdk
            sdk_image=localhost/kobuild:kindle-pw3
            abi=soft-float; interpreter=/lib/ld-linux.so.3; qemu_cpu=cortex-a9
            firmware=update_kindle_all_new_paperwhite_5.13.5.bin
            firmware_sha=943796120a94f96743f9c34d59b9727a21c91a15e61ef91f9b23159c6d045775
            firmware_size=253013186
            ;;
        *) echo "Unknown Kindle target: $target (choose pw5 or pw3)" >&2; return 2 ;;
    esac
}

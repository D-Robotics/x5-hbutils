#!/bin/sh
#Copyright: D-Robotic
#Function: X5 NOC QoS tool - Fixed / Limiter / Regulator modes

set -eu

QOS_BASE="${QOS_BASE:-/sys/bus/platform/drivers/noc_qos}"
QOS_BW_REG_MAX=4095
QOS_SAT_REG_MAX=1023

# module_id|alias|group|freq_mhz
# freq_mhz: used by calc/apply for Limiter and Regulator (bandwidth_reg = 256 * BW / F)
QOS_MODULES_CONF_DATA=$(cat <<'QOS_MODULES_EOF'
# module_id|alias|group|freq_mhz
# freq_mhz: used by calc/apply for Limiter and Regulator (bandwidth_reg = 256 * BW / F)
# Refer to X5 QoS guide register tables when tuning; values here are typical defaults.
20500000.cpu_ace_qos|cpu_ace|CPU|1200
20500100.cpu_perif_qos|cpu_perif|CPU|1200
20510000.bt1120_qos|bt1120|VIN|600
20510080.dc8000_qos|dc8000|VIN|600
20510100.dw230_gdc_qos|gdc|VIN|600
20510180.dw230_scalar2_qos|vse_scalar2|VIN|600
20510200.dw230_scalar3_qos|vse_scalar3|VIN|600
20510280.isp_axi5_hdr_qos|isp_hdr|VIN|600
20510300.isp_axi4_mcm_qos|isp_mcm|VIN|600
20510380.isp_axi3_sp2_qos|isp_sp2|VIN|600
20510480.isp_axi1_mp_qos|isp_mp|VIN|600
20510500.sif0_qos|sif0|VIN|600
20510580.sif1_qos|sif1|VIN|600
20510600.sif2_qos|sif2|VIN|600
20510680.sif3_qos|sif3|VIN|600
20510700.sif_disp_qos|sif_disp|VIN|600
20520000.bpu_qos|bpu|BPU|1000
20530000.video_qos|video|CODEC|600
20530080.jpeg_qos|jpeg|CODEC|600
20540000.gpu2d_qos|gpu2d|GPU|1000
20540080.gpu3d_qos|gpu3d|GPU|1000
20550000.dma0_qos|dma0|HSIO|600
20550080.emmc_qos|emmc|HSIO|200
20550100.gmac_qos|gmac|HSIO|400
20550180.sd_qos|sd|HSIO|200
20550200.sdio_qos|sdio|HSIO|200
20550280.security_qos|security|HSIO|600
20550300.usb2_qos|usb2|HSIO|400
20550380.usb3_qos|usb3|HSIO|400
20550400.etr_qos|etr|HSIO|600
20560000.hifi5_qos|hifi5|HIFI5|432
QOS_MODULES_EOF
)

qos_trim_field() {
	echo "$1" | sed 's/[[:space:]]//g'
}

qos_is_conf_comment() {
	trimmed=$(echo "$1" | sed 's/^[[:space:]]*//')
	case "$trimmed" in
	''|\#*) return 0 ;;
	esac
	return 1
}

qos_sysfs_path() {
	module_id=$1
	node=$2
	echo "${QOS_BASE}/${module_id}/${node}"
}

qos_read_priority_value() {
	path=$1
	if [ ! -f "$path" ]; then
		return 1
	fi
	awk '{print $NF}' "$path"
}

qos_module_probe() {
	module_id=$1
	base="${QOS_BASE}/${module_id}"

	if [ ! -d "$base" ]; then
		echo "missing"
		return 0
	fi
	if [ ! -f "${base}/read_priority_qos_ctrl/priority" ] ||
	   [ ! -f "${base}/write_priority_qos_ctrl/priority" ]; then
		echo "unsupported"
		return 0
	fi
	if [ -f "${base}/mode_qos_ctrl/mode" ]; then
		echo "full"
	else
		echo "fixed_only"
	fi
}

qos_resolve_module() {
	name=$1
	id=
	alias=
	match=

	if [ -z "$name" ]; then
		echo "error: empty module name" >&2
		return 1
	fi

	if [ -d "${QOS_BASE}/${name}" ]; then
		echo "$name"
		return 0
	fi

	while IFS='|' read -r id alias _rest; do
		if qos_is_conf_comment "$id"; then
			continue
		fi
		if [ "$alias" = "$name" ]; then
			echo "$id"
			return 0
		fi
	done <<EOF
${QOS_MODULES_CONF_DATA}
EOF

	match=$(find "${QOS_BASE}" -maxdepth 1 -type d -name "*${name}*_qos" 2>/dev/null | head -n 1)
	if [ -n "$match" ]; then
		basename "$match"
		return 0
	fi

	echo "error: module '${name}' not found under ${QOS_BASE}" >&2
	return 1
}

qos_write_node() {
	path=$1
	value=$2

	if [ ! -f "$path" ]; then
		echo "error: missing node ${path}" >&2
		return 1
	fi
	if ! echo "$value" > "$path"; then
		echo "error: write '${value}' to ${path} failed" >&2
		return 1
	fi
}

qos_verify_priority() {
	module_id=$1
	expect_read=$2
	expect_write=$3
	read_v=
	write_v=

	read_v=$(qos_read_priority_value "$(qos_sysfs_path "$module_id" "read_priority_qos_ctrl/priority")")
	write_v=$(qos_read_priority_value "$(qos_sysfs_path "$module_id" "write_priority_qos_ctrl/priority")")

	if [ "$read_v" != "$expect_read" ]; then
		echo "error: ${module_id} read priority verify fail (${expect_read} != ${read_v})" >&2
		return 1
	fi
	if [ "$write_v" != "$expect_write" ]; then
		echo "error: ${module_id} write priority verify fail (${expect_write} != ${write_v})" >&2
		return 1
	fi
	return 0
}

qos_verify_mode_field() {
	module_id=$1
	field=$2
	expect=$3
	path=
	val=

	path=$(qos_sysfs_path "$module_id" "mode_qos_ctrl/${field}")
	if [ ! -f "$path" ]; then
		return 0
	fi

	val=$(qos_read_priority_value "$path")
	if [ "$val" != "$expect" ]; then
		echo "error: ${module_id} ${field} verify fail (${expect} != ${val})" >&2
		return 1
	fi
	return 0
}

qos_show_module() {
	module_id=$1
	verbose=$2
	cap=
	base=
	read_p=
	write_p=

	cap=$(qos_module_probe "$module_id")
	base="${QOS_BASE}/${module_id}"

	if [ "$cap" = "missing" ]; then
		echo "${module_id}: not present"
		return 1
	fi

	read_p=$(qos_read_priority_value "${base}/read_priority_qos_ctrl/priority")
	write_p=$(qos_read_priority_value "${base}/write_priority_qos_ctrl/priority")

	case "$cap" in
	fixed_only)
		printf "%-32s [fixed_only] read=%s write=%s\n" "$module_id" "$read_p" "$write_p"
		;;
	unsupported)
		printf "%-32s [unsupported] read=%s write=%s\n" "$module_id" "$read_p" "$write_p"
		;;
	*)
		printf "%-32s read=%s write=%s\n" "$module_id" "$read_p" "$write_p"
		;;
	esac

	if [ "$verbose" = "1" ] && [ "$cap" = "full" ]; then
		mode_val=$(qos_read_priority_value "${base}/mode_qos_ctrl/mode")
		mode_name=$(qos_mode_val_to_name "$mode_val")
		printf "\t\t\tadvanced=%s mode=%s(%s) bandwidth=%s saturation=%s extcontrol=%s\n" \
			"$(qos_read_priority_value "${base}/mode_qos_ctrl/advanced")" \
			"$mode_val" "$mode_name" \
			"$(qos_read_priority_value "${base}/mode_qos_ctrl/bandwidth")" \
			"$(qos_read_priority_value "${base}/mode_qos_ctrl/saturation")" \
			"$(qos_read_priority_value "${base}/mode_qos_ctrl/extcontrol")"
	fi
}

qos_list_modules() {
	verbose=$1
	dir=

	if [ ! -d "$QOS_BASE" ]; then
		echo "error: ${QOS_BASE} not found, is noc_qos driver loaded?" >&2
		return 1
	fi

	for dir in "${QOS_BASE}"/*qos*/; do
		if [ ! -d "$dir" ]; then
			continue
		fi
		qos_show_module "$(basename "$dir")" "$verbose"
	done
}

qos_get_module_freq_mhz() {
	module_id=$1
	id=
	_alias=
	freq=

	while IFS='|' read -r id _alias _group freq; do
		if qos_is_conf_comment "$id"; then
			continue
		fi
		if [ "$id" = "$module_id" ] && [ -n "$freq" ]; then
			echo "$freq"
			return 0
		fi
	done <<EOF
${QOS_MODULES_CONF_DATA}
EOF

	return 1
}

qos_calc_bandwidth_reg() {
	bw_mbps=$1
	freq_mhz=$2
	reg=

	if [ -z "$bw_mbps" ] || [ -z "$freq_mhz" ] || [ "$freq_mhz" -le 0 ]; then
		echo "error: invalid bw_mbps=${bw_mbps} or freq_mhz=${freq_mhz}" >&2
		return 1
	fi

	reg=$((256 * bw_mbps / freq_mhz))
	if [ "$reg" -gt "$QOS_BW_REG_MAX" ]; then
		echo "error: bandwidth register ${reg} exceeds max ${QOS_BW_REG_MAX}" >&2
		return 1
	fi
	echo "$reg"
}

# threshold_bytes = saturation_reg * 16
qos_calc_saturation_reg_from_bytes() {
	bytes=$1
	reg=

	if [ -z "$bytes" ] || [ "$bytes" -le 0 ]; then
		echo "error: invalid threshold bytes ${bytes}" >&2
		return 1
	fi

	reg=$((bytes / 16))
	if [ "$reg" -gt "$QOS_SAT_REG_MAX" ]; then
		echo "error: saturation register ${reg} exceeds max ${QOS_SAT_REG_MAX}" >&2
		echo "hint: use smaller threshold or set saturation register directly in profile" >&2
		return 1
	fi
	echo "$reg"
}

qos_calc_saturation_reg_from_kb() {
	kb=$1
	qos_calc_saturation_reg_from_bytes $((kb * 1024))
}

qos_mode_name_to_val() {
	mode=$1

	case "$(echo "$mode" | tr '[:upper:]' '[:lower:]')" in
	fixed) echo 0 ;;
	limiter) echo 1 ;;
	bypass) echo 2 ;;
	regulator) echo 3 ;;
	0|1|2|3) echo "$mode" ;;
	*)
		echo "error: unknown mode '${mode}'" >&2
		return 1
		;;
	esac
}

qos_mode_val_to_name() {
	case "$1" in
	0) echo "fixed" ;;
	1) echo "limiter" ;;
	2) echo "bypass" ;;
	3) echo "regulator" ;;
	*) echo "unknown($1)" ;;
	esac
}

qos_print_calc_result() {
	module_id=$1
	bw_mbps=$2
	freq_mhz=$3
	threshold_kb=$4
	bw_reg=
	sat_reg=

	bw_reg=$(qos_calc_bandwidth_reg "$bw_mbps" "$freq_mhz") || return 1

	echo "module     : ${module_id}"
	echo "freq_mhz   : ${freq_mhz}"
	echo "bw_mbps    : ${bw_mbps}"
	echo "bandwidth  : ${bw_reg} (0x$(printf '%x' "$bw_reg"))"

	if [ -n "$threshold_kb" ]; then
		sat_reg=$(qos_calc_saturation_reg_from_kb "$threshold_kb") || return 1
		echo "threshold  : ${threshold_kb} KB (${threshold_kb}*1024 bytes)"
		echo "saturation : ${sat_reg} (0x$(printf '%x' "$sat_reg"))"
	fi
}

qos_apply_fixed() {
	module_id=$1
	read_p=$2
	write_p=$3
	cap=

	cap=$(qos_module_probe "$module_id")
	if [ "$cap" = "missing" ] || [ "$cap" = "unsupported" ]; then
		echo "error: ${module_id} cannot apply fixed (${cap})" >&2
		return 1
	fi

	qos_write_node "$(qos_sysfs_path "$module_id" "write_priority_qos_ctrl/priority")" "$write_p" || return 1
	qos_write_node "$(qos_sysfs_path "$module_id" "read_priority_qos_ctrl/priority")" "$read_p" || return 1
	qos_verify_priority "$module_id" "$read_p" "$write_p" || return 1

	echo "ok: ${module_id} mode=fixed read=${read_p} write=${write_p}"
}

qos_apply_complex() {
	module_id=$1
	mode_val=$2
	read_p=$3
	write_p=$4
	bw_reg=$5
	sat_reg=$6
	extcontrol=${7:-0}
	cap=
	mode_name=

	cap=$(qos_module_probe "$module_id")
	if [ "$cap" != "full" ]; then
		echo "error: ${module_id} does not support complex mode (${cap})" >&2
		return 1
	fi

	mode_name=$(qos_mode_val_to_name "$mode_val")

	qos_write_node "$(qos_sysfs_path "$module_id" "mode_qos_ctrl/advanced")" 1 || return 1
	qos_write_node "$(qos_sysfs_path "$module_id" "mode_qos_ctrl/mode")" "$mode_val" || return 1

	if [ -n "$bw_reg" ]; then
		qos_write_node "$(qos_sysfs_path "$module_id" "mode_qos_ctrl/bandwidth")" "$bw_reg" || return 1
	fi
	if [ -n "$sat_reg" ]; then
		qos_write_node "$(qos_sysfs_path "$module_id" "mode_qos_ctrl/saturation")" "$sat_reg" || return 1
	fi
	qos_write_node "$(qos_sysfs_path "$module_id" "mode_qos_ctrl/extcontrol")" "$extcontrol" || return 1

	qos_write_node "$(qos_sysfs_path "$module_id" "write_priority_qos_ctrl/priority")" "$write_p" || return 1
	qos_write_node "$(qos_sysfs_path "$module_id" "read_priority_qos_ctrl/priority")" "$read_p" || return 1

	qos_verify_mode_field "$module_id" "advanced" 1 || return 1
	qos_verify_mode_field "$module_id" "mode" "$mode_val" || return 1
	qos_verify_priority "$module_id" "$read_p" "$write_p" || return 1

	echo "ok: ${module_id} mode=${mode_name} read=${read_p} write=${write_p} bw=${bw_reg} sat=${sat_reg} ext=${extcontrol}"
}

qos_apply_limiter() {
	module_id=$1
	read_p=$2
	write_p=$3
	bw_reg=$4
	sat_reg=$5
	extcontrol=${6:-0}

	if [ -z "$bw_reg" ]; then
		echo "error: limiter requires bandwidth register value" >&2
		return 1
	fi

	qos_apply_complex "$module_id" 1 "$read_p" "$write_p" "$bw_reg" "$sat_reg" "$extcontrol"
}

qos_apply_regulator() {
	module_id=$1
	p0=$2
	p1=$3
	bw_reg=$4
	sat_reg=$5
	extcontrol=${6:-0}

	if [ "$p1" -lt "$p0" ]; then
		echo "error: regulator requires P1(high) >= P0(low), got P0=${p0} P1=${p1}" >&2
		return 1
	fi

	# Regulator: P0=write (low), P1=read (high)
	qos_apply_complex "$module_id" 3 "$p1" "$p0" "$bw_reg" "$sat_reg" "$extcontrol"
}

qos_apply_bypass() {
	module_id=$1
	read_p=$2
	write_p=$3
	bw_reg=$4
	sat_reg=$5
	extcontrol=${6:-0}

	if [ -z "$bw_reg" ]; then
		echo "error: bypass requires bandwidth register value" >&2
		return 1
	fi

	qos_apply_complex "$module_id" 2 "$read_p" "$write_p" "$bw_reg" "$sat_reg" "$extcontrol"
}

qos_apply_profile_line() {
	module_id=$1
	mode=$2
	read_p=$3
	write_p=$4
	bw_mbps=$5
	threshold_kb=$6
	bw_reg=$7
	sat_reg=$8
	extcontrol=${9:-0}
	freq_mhz=

	case "$(echo "$mode" | tr '[:upper:]' '[:lower:]')" in
	fixed)
		qos_apply_fixed "$module_id" "$read_p" "$write_p"
		;;
	limiter)
		if [ -z "$bw_reg" ] && [ -n "$bw_mbps" ]; then
			freq_mhz=$(qos_get_module_freq_mhz "$module_id")
			if [ -z "$freq_mhz" ]; then
				echo "error: ${module_id} has no freq_mhz in QOS_MODULES_CONF_DATA for bw calc" >&2
				return 1
			fi
			bw_reg=$(qos_calc_bandwidth_reg "$bw_mbps" "$freq_mhz") || return 1
		fi
		if [ -z "$bw_reg" ]; then
			echo "error: ${module_id} limiter needs bw_mbps or bw_reg" >&2
			return 1
		fi

		if [ -z "$sat_reg" ] && [ -n "$threshold_kb" ]; then
			sat_reg=$(qos_calc_saturation_reg_from_kb "$threshold_kb") || return 1
		fi

		qos_apply_limiter "$module_id" "$read_p" "$write_p" "$bw_reg" "$sat_reg" "$extcontrol"
		;;
	regulator)
		if [ -z "$bw_reg" ] && [ -n "$bw_mbps" ]; then
			freq_mhz=$(qos_get_module_freq_mhz "$module_id")
			if [ -z "$freq_mhz" ]; then
				echo "error: ${module_id} has no freq_mhz in QOS_MODULES_CONF_DATA for bw calc" >&2
				return 1
			fi
			bw_reg=$(qos_calc_bandwidth_reg "$bw_mbps" "$freq_mhz") || return 1
		fi
		if [ -z "$bw_reg" ]; then
			echo "error: ${module_id} regulator needs bw_mbps or bw_reg" >&2
			return 1
		fi

		if [ -z "$sat_reg" ] && [ -n "$threshold_kb" ]; then
			sat_reg=$(qos_calc_saturation_reg_from_kb "$threshold_kb") || return 1
		fi

		# profile: read_p=P1(high), write_p=P0(low)
		qos_apply_regulator "$module_id" "$write_p" "$read_p" "$bw_reg" "$sat_reg" "$extcontrol"
		;;
	bypass)
		if [ -z "$bw_reg" ]; then
			echo "error: ${module_id} bypass needs bw_reg in profile" >&2
			return 1
		fi

		if [ -z "$sat_reg" ] && [ -n "$threshold_kb" ]; then
			sat_reg=$(qos_calc_saturation_reg_from_kb "$threshold_kb") || return 1
		fi

		qos_apply_bypass "$module_id" "$read_p" "$write_p" "$bw_reg" "${sat_reg:-0}" "$extcontrol"
		;;
	*)
		echo "error: unknown mode '${mode}' in profile" >&2
		return 1
		;;
	esac
}

qos_save_module() {
	module_id=$1
	cap=
	mode=
	read_p=
	write_p=
	bw=
	sat=
	ext=

	cap=$(qos_module_probe "$module_id")
	if [ "$cap" = "missing" ]; then
		return 1
	fi

	read_p=$(qos_read_priority_value "$(qos_sysfs_path "$module_id" "read_priority_qos_ctrl/priority")")
	write_p=$(qos_read_priority_value "$(qos_sysfs_path "$module_id" "write_priority_qos_ctrl/priority")")

	if [ "$cap" = "fixed_only" ]; then
		printf "%s|fixed|%s|%s||||0\n" "$module_id" "$read_p" "$write_p"
		return 0
	fi

	mode=$(qos_read_priority_value "$(qos_sysfs_path "$module_id" "mode_qos_ctrl/mode")")
	bw=$(qos_read_priority_value "$(qos_sysfs_path "$module_id" "mode_qos_ctrl/bandwidth")")
	sat=$(qos_read_priority_value "$(qos_sysfs_path "$module_id" "mode_qos_ctrl/saturation")")
	ext=$(qos_read_priority_value "$(qos_sysfs_path "$module_id" "mode_qos_ctrl/extcontrol")")

	if [ "$mode" = "0" ]; then
		printf "%s|fixed|%s|%s||||0\n" "$module_id" "$read_p" "$write_p"
	else
		printf "%s|%s|%s|%s|||%s|%s|%s\n" \
			"$module_id" "$(qos_mode_val_to_name "$mode")" "$read_p" "$write_p" "$bw" "$sat" "$ext"
	fi
}

qos_usage() {
	cat <<'EOF'
Usage:
  qos_tool.sh list [-a]
  qos_tool.sh show [module...] [-a]
  qos_tool.sh set <module> fixed <read_p> <write_p>
  qos_tool.sh set <module> limiter <read_p> <write_p> <bw_mbps> <threshold_kb>
  qos_tool.sh set <module> regulator <p0_low> <p1_high> <bw_mbps> <threshold_kb>
  qos_tool.sh apply <profile.conf>
  qos_tool.sh save [-o file.conf] [module...]
  qos_tool.sh calc <module> <bw_mbps> [threshold_kb] [--freq mhz]

Profile format (pipe-separated, # for comments):
  module_id|mode|read_p|write_p|bw_mbps|threshold_kb|bw_reg|sat_reg|extcontrol

Notes:
  - module can be alias (e.g. sif0) or full id (20510500.sif0_qos)
  - Regulator: profile read_p=P1(high), write_p=P0(low)
  - set limiter/regulator requires threshold_kb; maps to saturation (bytes/16), max register 1023
  - extcontrol is optional (default 0); save output includes it for backup, apply accepts it
  - Requires root to write sysfs under /sys/bus/platform/drivers/noc_qos
EOF
}

qos_cmd_list() {
	verbose=0
	if [ "${1:-}" = "-a" ]; then
		verbose=1
	fi
	qos_list_modules "$verbose"
}

qos_cmd_show() {
	verbose=0
	has_module=0
	arg=
	module_id=
	module_ids=

	for arg in "$@"; do
		if [ "$arg" = "-a" ]; then
			verbose=1
		else
			has_module=1
			module_id=$(qos_resolve_module "$arg") || return 1
			module_ids="${module_ids} ${module_id}"
		fi
	done

	if [ "$has_module" -eq 0 ]; then
		qos_list_modules "$verbose"
		return 0
	fi

	for module_id in $module_ids; do
		qos_show_module "$module_id" "$verbose"
	done
}

qos_cmd_set() {
	module_name=$1
	mode=$2
	shift 2

	module_id=
	freq_mhz=
	bw_reg=
	sat_reg=

	module_id=$(qos_resolve_module "$module_name") || return 1

	case "$(echo "$mode" | tr '[:upper:]' '[:lower:]')" in
	fixed)
		if [ $# -lt 2 ]; then
			echo "error: set fixed needs <read_p> <write_p>" >&2
			return 1
		fi
		qos_apply_fixed "$module_id" "$1" "$2"
		;;
	limiter)
		if [ $# -lt 4 ]; then
			echo "error: set limiter needs <read_p> <write_p> <bw_mbps> <threshold_kb>" >&2
			return 1
		fi
		freq_mhz=$(qos_get_module_freq_mhz "$module_id") || {
			echo "error: no freq for ${module_id}, add to QOS_MODULES_CONF_DATA in qos_tool.sh, or use calc --freq <mhz> / apply with bw_reg" >&2
			return 1
		}
		bw_reg=$(qos_calc_bandwidth_reg "$3" "$freq_mhz") || return 1
		sat_reg=$(qos_calc_saturation_reg_from_kb "$4") || return 1
		qos_apply_limiter "$module_id" "$1" "$2" "$bw_reg" "$sat_reg"
		;;
	regulator)
		if [ $# -lt 4 ]; then
			echo "error: set regulator needs <p0_low> <p1_high> <bw_mbps> <threshold_kb>" >&2
			return 1
		fi
		freq_mhz=$(qos_get_module_freq_mhz "$module_id") || return 1
		bw_reg=$(qos_calc_bandwidth_reg "$3" "$freq_mhz") || return 1
		sat_reg=$(qos_calc_saturation_reg_from_kb "$4") || return 1
		qos_apply_regulator "$module_id" "$1" "$2" "$bw_reg" "$sat_reg"
		;;
	*)
		echo "error: unknown mode '${mode}'" >&2
		return 1
		;;
	esac
}

qos_cmd_apply() {
	profile=$1
	rc=0
	count=0
	module_id=
	mode=
	read_p=
	write_p=
	bw_mbps=
	threshold_kb=
	bw_reg=
	sat_reg=
	extcontrol=

	if [ ! -f "$profile" ]; then
		echo "error: profile not found: ${profile}" >&2
		return 1
	fi

	while IFS='|' read -r module_id mode read_p write_p bw_mbps threshold_kb bw_reg sat_reg extcontrol; do
		if qos_is_conf_comment "$module_id"; then
			continue
		fi

		module_id=$(qos_trim_field "$module_id")
		mode=$(qos_trim_field "$mode")

		if ! qos_apply_profile_line "$module_id" "$mode" "$read_p" "$write_p" \
			"$bw_mbps" "$threshold_kb" "$bw_reg" "$sat_reg" "${extcontrol:-0}"; then
			rc=1
		fi
		count=$((count + 1))
	done < "$profile"

	echo "apply done: ${count} entries, exit=${rc}"
	return "$rc"
}

qos_cmd_save() {
	outfile=
	save_args=
	has_module=0
	arg=
	module_id=

	while [ $# -gt 0 ]; do
		case "$1" in
		-o)
			outfile=$2
			shift 2
			;;
		*)
			has_module=1
			save_args="${save_args} $1"
			shift
			;;
		esac
	done

	if [ -z "$outfile" ]; then
		outfile="/tmp/qos_backup_$(date +%Y%m%d_%H%M%S).conf"
	fi

	{
		echo "# QoS backup generated by qos_tool.sh"
		echo "# module_id|mode|read_p|write_p|bw_mbps|threshold_kb|bw_reg|sat_reg|extcontrol"
		if [ "$has_module" -eq 0 ]; then
			for dir in "${QOS_BASE}"/*qos*/; do
				if [ ! -d "$dir" ]; then
					continue
				fi
				qos_save_module "$(basename "$dir")"
			done
		else
			for arg in $save_args; do
				module_id=$(qos_resolve_module "$arg") || return 1
				qos_save_module "$module_id"
			done
		fi
	} > "$outfile"

	echo "saved to ${outfile}"
}

qos_cmd_calc() {
	module_name=$1
	bw_mbps=$2
	threshold_kb=
	freq_mhz=
	module_id=

	shift 2
	while [ $# -gt 0 ]; do
		case "$1" in
		--freq)
			freq_mhz=$2
			shift 2
			;;
		*)
			threshold_kb=$1
			shift
			;;
		esac
	done

	module_id=$(qos_resolve_module "$module_name") || return 1

	if [ -z "$freq_mhz" ]; then
		freq_mhz=$(qos_get_module_freq_mhz "$module_id") || {
			echo "error: no freq for ${module_id}, use --freq <mhz>" >&2
			return 1
		}
	fi

	qos_print_calc_result "$module_id" "$bw_mbps" "$freq_mhz" "$threshold_kb"
}

main() {
	cmd=${1:-}

	if [ -z "$cmd" ] || [ "$cmd" = "-h" ] || [ "$cmd" = "--help" ]; then
		qos_usage
		exit 0
	fi
	shift

	case "$cmd" in
	list)
		qos_cmd_list ${1:-}
		;;
	show)
		qos_cmd_show "$@"
		;;
	set)
		if [ $# -lt 3 ]; then
			qos_usage
			exit 1
		fi
		qos_cmd_set "$@"
		;;
	apply)
		if [ $# -lt 1 ]; then
			echo "error: apply needs profile path" >&2
			exit 1
		fi
		qos_cmd_apply "$1"
		;;
	save)
		qos_cmd_save "$@"
		;;
	calc)
		if [ $# -lt 2 ]; then
			echo "error: calc needs module and bw_mbps" >&2
			exit 1
		fi
		qos_cmd_calc "$@"
		;;
	*)
		echo "error: unknown command '${cmd}'" >&2
		qos_usage
		exit 1
		;;
	esac
}

main "$@"

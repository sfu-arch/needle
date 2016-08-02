#!/usr/bin/env python

from collections import defaultdict, deque
from functools import partial
from multiprocessing import Pool
import argparse
import csv
import glob
import gzip
import os
import os.path
import sys


PREDICTOR = '/home/wsumner/predictor'

PATH_PROFILE_BASE = '/p-ssd/ska124/run-framework-superblock'


TASKSX = [
    (20 ,'/p-ssd/ska124/run-framework-rle/164.gzip/longest_match/path-profile-trace.gz'),
    (54 ,'/p-ssd/ska124/run-framework-rle/175.vpr/try_route/path-profile-trace.gz'),
    (69 ,'/p-ssd/ska124/run-framework-rle/179.art/match/path-profile-trace.gz'),
    (77 ,'/p-ssd/ska124/run-framework-rle/181.mcf/price_out_impl/path-profile-trace.gz'),
    (5 ,'/p-ssd/ska124/run-framework-rle/183.equake/smvp/path-profile-trace.gz'),
    (3781482737149899079 ,'/p-ssd/ska124/run-framework-rle/186.crafty/EvaluatePawns/path-profile-trace.gz'),
    (12 ,'/p-ssd/ska124/run-framework-rle/197.parser/table_pointer/path-profile-trace.gz'),
    (10746002067 ,'/p-ssd/ska124/run-framework-rle/401.bzip2/BZ2_compressBlock/path-profile-trace.gz'),
    (2337 ,'/p-ssd/ska124/run-framework-rle/403.gcc/bitmap_operation/path-profile-trace.gz'),
    (74 ,'/p-ssd/ska124/run-framework-rle/429.mcf/price_out_impl/path-profile-trace.gz'),
    (346 ,'/p-ssd/ska124/run-framework-rle/444.namd/_ZN20ComputeNonbondedUtil26calc_pair_energy_fullelectEP9nonbonded/path-profile-trace.gz'),
    (0 ,'/p-ssd/ska124/run-framework-rle/450.soplex/_ZN6soplex9CLUFactor16vSolveUrightNoNZEPdS1_Piid/path-profile-trace.gz'),
    (1749 ,'/p-ssd/ska124/run-framework-rle/453.povray/_ZN3povL24All_Sphere_IntersectionsEPNS_13Object_StructEPNS_10Ray_StructEPNS_13istack_structE/path-profile-trace.gz'),
    (12408 ,'/p-ssd/ska124/run-framework-rle/456.hmmer/P7Viterbi/path-profile-trace.gz'),
    (18920763223 ,'/p-ssd/ska124/run-framework-rle/458.sjeng/gen/path-profile-trace.gz'),
    (46 ,'/p-ssd/ska124/run-framework-rle/464.h264ref/dct_luma_16x16/path-profile-trace.gz'),
    (0 ,'/p-ssd/ska124/run-framework-rle/470.lbm/LBM_performStreamCollide/path-profile-trace.gz'),
    (12 ,'/p-ssd/ska124/run-framework-rle/482.sphinx3/vector_gautbl_eval_logs3/path-profile-trace.gz'),
    ]
TASKS = [
    (1502915 ,'/p-ssd/ska124/run-framework-rle/blackscholes/_Z19BlkSchlsEqEuroNoDivfffffif/path-profile-trace.gz'),
    ]
TASKSC = [
    (0 ,'/p-ssd/ska124/run-framework-rle/bodytrack/_ZN17ImageMeasurements11InsideErrorERK17ProjectedCylinderRK11BinaryImageRiS6_/path-profile-trace.gz'),
    (6 ,'/p-ssd/ska124/run-framework-rle/dwt53/dwt53_row_transpose/path-profile-trace.gz'),
    (205 ,'/p-ssd/ska124/run-framework-rle/ferret/image_segment/path-profile-trace.gz'),
    (313317 ,'/p-ssd/ska124/run-framework-rle/fft-2d/fft/path-profile-trace.gz'),
    (46 ,'/p-ssd/ska124/run-framework-rle/fluidanimate/_Z13ComputeForcesv/path-profile-trace.gz'),
    (16 ,'/p-ssd/ska124/run-framework-rle/freqmine/_Z32FPArray_conditional_pattern_baseIhEiP7FP_treeiiT_/path-profile-trace.gz'),
    (5124595 ,'/p-ssd/ska124/run-framework-rle/sar-backprojection/sar_backprojection/path-profile-trace.gz'),
    (394 ,'/p-ssd/ska124/run-framework-rle/sar-pfa-interp1/sar_interp1/path-profile-trace.gz'),
    (121 ,'/p-ssd/ska124/run-framework-rle/streamcluster/_Z5pgainlP6PointsdPliP17pthread_barrier_t/path-profile-trace.gz'),
    (200727416487125835483 ,'/p-ssd/ska124/run-framework-rle/swaptions/_Z21HJM_Swaption_BlockingPddddddiidS_PS_llii/path-profile-trace.gz'),
]


def get_task_info(task_number):
    in_hex = hex(TASKS[task_number][0])[2:]
    in_hex = in_hex if not in_hex[-1] == 'L' else in_hex[:-1]
    path   = TASKS[task_number][1]
    name   = path.split('/')[4]
    return (name,
            (32 - len(in_hex))*'0' + in_hex,
            name + '-prediction-level%d.csv',
            path)


def to_path_ids(hex_ids):
    hex_ids = hex_ids.strip()
    return tuple(int(hex_ids[i:i+32],16) for i in range(0,len(hex_ids),32))


def get_path_sizes(name):
    epp_pattern = os.path.join(PATH_PROFILE_BASE, name, '*', 'epp-sequences.txt')
    (epp_file,) = glob.glob(epp_pattern)
    sizes = {}
    with open(epp_file, 'r') as infile:
        for line in infile:
            cols = line.strip().split(' ')
            sizes[int(cols[0])] = int(cols[3])
    return sizes


# Return a map from path ids to the sequence of blocks in that path
def get_path_blocks(name):
    epp_pattern = os.path.join(PATH_PROFILE_BASE, name, '*', 'epp-sequences.txt')
    (epp_file,) = glob.glob(epp_pattern)
    ids         = {}
    path_blocks = {}
    with open(epp_file, 'r') as infile:
        for line in infile:
            cols   = line.strip().split(' ')
            blocks = []
            for block in cols[4:]:
                if block not in ids:
                    ids[block] = len(ids)
                blocks.append(ids[block])
            path_blocks[int(cols[0])] = tuple(blocks)
    return path_blocks


# For a given result file, returns:
#    1) The number of histories observed
#    2) The number of acceleratable histories starting with the target
#    3) The number of successful predictions using the history
#    4) The success ratio
#    5) The sequence of path IDs
def get_predictability_stats(name, target_path, result_file):
    with open(result_file) as infile:
        line          = infile.readline()
        num_histories = int(line.split()[-1])
        csvreader     = csv.reader(infile)
        raw_rows      = tuple(row for row in csvreader)

    path_sizes  = get_path_sizes(name)
    target_path = int(target_path, 16)

    # filter the candidate path sequences to only include the histories
    # starting with the target and where the history and next path are all
    # acceleratable.
    rows = []
    for history, predicted, total, next_path in raw_rows:
        history   = to_path_ids(history)
        next_path = to_path_ids(next_path)
        if not history[0] == target_path:
            continue
        if not all((x in path_sizes) for x in (history + next_path)):
            print >> sys.stderr, 'Skip non accel:',history,next_path
            continue
        num_ops = sum(path_sizes[x] for x in (history + next_path))
        rows.append((history, int(predicted), int(total), next_path, num_ops))
        
    total_entries = sum(row[2] for row in rows) if rows else 1

    # Because the total is the same for all, we use num ops * predicts for each path
    weightiest    = max(rows, key=lambda row: row[1]*row[4]) if rows else ((),0,0,(),0)
    history       = weightiest[0]
    next_path     = weightiest[3]

    return (num_histories, total_entries, weightiest[1],
            weightiest[1]/float(total_entries), weightiest[4]) + history + next_path


def print_leveled_stats(history_size):
    for name, target_path, csv, _ in (get_task_info(i) for i in range(len(TASKS))):
        result_file = csv % (history_size,)
        if os.path.isfile(result_file) and os.stat(result_file).st_size:
            print ','.join(str(x) for x in (name,) + get_predictability_stats(name, target_path, result_file))


def create_prediction_csv(task_number, level):
    name, target_path, csv, trace_path = get_task_info(task_number)
    result_file  = csv % (level,)
    command_args = (trace_path, PREDICTOR, level, target_path, result_file)
    command_line = 'zcat %s | %s %d %s > %s' % command_args
    os.system(command_line)


def create_leveled_csvs(history_size):
    leveled = partial(create_prediction_csv, level=history_size)
    pool    = Pool(processes=15)
    for i in pool.imap(leveled, range(len(TASKS))):
        # Including a pass here makes python play nicer with output when you
        # want to print something.
        pass


def create_pip_csv(task_number, level):
    NUM_ENTRIES = 16
    THRESHOLD   = 100000000
    name, target_path, csv, trace_path = get_task_info(task_number)

    path_blocks = get_path_blocks(name)
    target_path = int(target_path, 16)
    last_blocks = deque(maxlen=level)
    to_target   = set()
    to_all      = defaultdict(lambda: defaultdict(int))
    tracking    = False
    track_count = 0

    with gzip.open(trace_path) as infile:
        for line in infile:
            id, length = line.split()
            id         = int(id,16)
            length     = int(length)
            tracking  |= id == target_path
            if not tracking:
                if id in path_blocks:
                    last_blocks.append(length * path_blocks[id])
                continue
            if track_count >= THRESHOLD:
                break
            for i in range(length):
                history = tuple(last_blocks)
                if id == target_path:
                   to_target.add(history)
                to_all[history][id] += 1

                if id in path_blocks:
                    last_blocks.append(path_blocks[id])
                track_count += 1
                if track_count >= THRESHOLD:
                    break

    total_hits     = sum(to_all[h][target_path] for h in to_target)
    ranked         = tuple(sorted(to_target, cmp=lambda x,y:-cmp(x,y), key=lambda x: to_all[x][target_path]))
    offloaded      = ranked[:min(NUM_ENTRIES,len(ranked))]
    good_offloads  = sum(to_all[h][target_path] for h in offloaded)
    total_offloads = sum(count for h in offloaded for count in to_all[h].values())
    result = (name, len(offloaded), total_hits, good_offloads, total_offloads,
              float(good_offloads)/total_offloads, float(good_offloads)/total_hits)
    print >> sys.stderr, 'DONE', result
    return result


def create_pip_csvs(history_size):
    leveled = partial(create_pip_csv, level=history_size)
    pool    = Pool(processes=28)
    for result in pool.imap(leveled, range(len(TASKS))):
        print ','.join(str(x) for x in result)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Create and process path prediction stats.')
    parser.add_argument('--create', dest='action', action='store_const',
                        const=create_leveled_csvs, default=print_leveled_stats,
                        help='Create prediction CSVs.')
    parser.add_argument('--pip', dest='action', action='store_const',
                        const=create_pip_csvs, default=print_leveled_stats,
                        help='Create PIP CSVs.')
    parser.add_argument('--size', type=int, nargs='?', default=2,
                        help='Size of prediction history.')

    args = parser.parse_args()
    args.action(args.size)
    

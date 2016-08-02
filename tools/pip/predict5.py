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
import traceback


PREDICTOR = '/home/wsumner/predictor'

PATH_PROFILE_BASE = '/p-ssd/ska124/run-framework-superblock'


TASKSEEDS = [
('164.gzip',  20),
('164.gzip',  17),
('164.gzip',  21),
('164.gzip',  9),
('164.gzip',  14),
('175.vpr',  54),
('175.vpr',  1530),
('175.vpr',  48),
('175.vpr',  59),
('175.vpr',  1593),
('179.art',  69),
('179.art',  60),
('179.art',  37),
('179.art',  32),
('179.art',  63),
('181.mcf',  77),
('181.mcf',  31),
('181.mcf',  46),
('181.mcf',  44),
('181.mcf',  34),
('183.equake',  5),
('183.equake',  4),
('183.equake',  0),
('183.equake',  7),
('183.equake',  2),
('186.crafty',  3781482737149900000),
('186.crafty',  5041975946816460000),
('186.crafty',  2520989527483190000),
('186.crafty',  1260496317816480000),
('186.crafty',  3108149913162),
('197.parser',  12),
('197.parser',  0),
('197.parser',  6),
('197.parser',  8),
('197.parser',  4),
('401.bzip2',  10746002067),
('401.bzip2',  11234),
('401.bzip2',  7164005704),
('401.bzip2',  3582009311),
('401.bzip2',  5170454080),
('403.gcc',  2337),
('403.gcc',  2396),
('403.gcc',  2793),
('403.gcc',  2781),
('403.gcc',  1470),
('429.mcf',  74),
('429.mcf',  31),
('429.mcf',  38),
('429.mcf',  34),
('429.mcf',  67),
('444.namd',  346),
('444.namd',  414),
('444.namd',  389),
('444.namd',  390),
('444.namd',  398),
('450.soplex',  0),
('450.soplex',  3),
('450.soplex',  4),
('450.soplex',  1),
('450.soplex',  2),
('453.povray',  1749),
('453.povray',  565),
('453.povray',  326),
('453.povray',  348),
('453.povray',  333),
('456.hmmer',  12408),
('456.hmmer',  11890),
('456.hmmer',  12409),
('456.hmmer',  12539),
('456.hmmer',  11938),
('458.sjeng',  18920763223),
('458.sjeng',  9442627181),
('458.sjeng',  9437825189),
('458.sjeng',  18925565215),
('458.sjeng',  14224407213),
('464.h264ref',  46),
('464.h264ref',  5),
('464.h264ref',  308),
('464.h264ref',  11),
('464.h264ref',  52),
('470.lbm',  0),
('470.lbm',  2),
('470.lbm',  3),
('482.sphinx3',  12),
('482.sphinx3',  11),
('482.sphinx3',  5),
('482.sphinx3',  13),
('482.sphinx3',  14),
('blackscholes',  1502915),
('blackscholes',  1502914),
('blackscholes',  1561870),
('blackscholes',  1560035),
('blackscholes',  1561871),
('bodytrack',  0),
('bodytrack',  1),
('bodytrack',  8),
('bodytrack',  220),
('bodytrack',  18),
('dwt53',  6),
('dwt53',  3),
('dwt53',  0),
('dwt53',  7),
('dwt53',  5),
('ferret',  205),
('ferret',  73353),
('ferret',  234),
('ferret',  212),
('ferret',  210),
('fft-2d',  313317),
('fft-2d',  7606330),
('fft-2d',  313318),
('fft-2d',  313320),
('fft-2d',  7606328),
('fluidanimate',  46),
('fluidanimate',  1190),
('fluidanimate',  867),
('fluidanimate',  38),
('fluidanimate',  45),
('freqmine',  16),
('freqmine',  9),
('freqmine',  25),
('freqmine',  5),
('freqmine',  4),
('sar-backprojection',  5124595),
('sar-backprojection',  943016),
('sar-backprojection',  5678859),
('sar-backprojection',  7338916),
('sar-backprojection',  5124805),
('sar-pfa-interp1',  394),
('sar-pfa-interp1',  426),
('sar-pfa-interp1',  406),
('sar-pfa-interp1',  434),
('sar-pfa-interp1',  374),
('streamcluster',  121),
('streamcluster',  120),
('streamcluster',  124),
('streamcluster',  111),
('streamcluster',  68),
('swaptions',  200727416487126000000),
('swaptions',  200727416487126000000),
('swaptions',  200727416487126000000),
('swaptions',  200727416487126000000),
('swaptions',  2878400181745660000000),
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
  try:
    NUM_ENTRIES = 8
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
    #print >> sys.stderr, 'DONE', result
    return result
  except Exception as e:
    print >> sys.stderr, 'ERROR', name, target_path, traceback.format_exc()
    try:
      return (name, len(offloaded), total_hits, good_offloads, total_offloads, 0, 0)
    except:
      return (name, 0, 0, 0, 0, 0, 0)


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

    TASKS = []
    for name, path_id in TASKSEEDS:
        trace_pattern = os.path.join('/p-ssd/ska124/run-framework-rle', name, '*', 'path-profile-trace.gz')
        (trace_path,) = glob.glob(trace_pattern)
        TASKS.append((path_id, trace_path))

    args = parser.parse_args()
    args.action(args.size)
    

#!/usr/bin/python

import sys, pprint, operator

def main(filename):
    paths = [] 
    totalCov = 0
    with open(filename, 'r') as f:
        for l in f:
            s = l.strip().split(' ')
            paths.append({
                'pid' : s[0],
                'fqs' : int(s[1]),
                'ops' : int(s[3]),
                'bbs' : s[4:],
                'cov' : int(s[1])*int(s[3])
            }) 
            totalCov += int(s[1])*int(s[3])

    paths = sorted(paths, key = lambda x: x['cov'], reverse=True)        

    pp = pprint.PrettyPrinter(indent=2)
    pp.pprint(paths[:5])

    for n in range(min([5, len(paths)])):
        with open('path-seq-'+str(n)+'.txt','w') as f:
            p = paths[n]
            f.write(' '.join([p['pid'], str(p['fqs']), str(3), str(p['ops'])]+p['bbs'] + ['\n']))
            

if __name__ == "__main__":
    main(sys.argv[1])

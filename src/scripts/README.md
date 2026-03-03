# Scripts Quick Usage

All scripts run from the project root at `src/` and accept optional arguments.
Python upload fixtures are stored in `src/testdata/`.

## Common pattern

- `port` defaults per script
- `pshome` defaults to `/tmp/pshome`
- If omitted, defaults are used

## Verify scripts

- `./scripts/verify_flow.sh [port] [pshome]`
  - Default: `./scripts/verify_flow.sh 18001 /tmp/pshome`

- `./scripts/verify_professor_subset.sh [port] [pshome]`
  - Default: `./scripts/verify_professor_subset.sh 18002 /tmp/pshome`

- `./scripts/verify_remaining_200s.sh [port] [pshome]`
  - Default: `./scripts/verify_remaining_200s.sh 18003 /tmp/pshome`

- `./scripts/verify_to_file.sh [port] [pshome] [out_file]`
  - Default: `./scripts/verify_to_file.sh 18004 /tmp/pshome /tmp/results18004.txt`

## Results / diagnostics

- `./scripts/show_results.sh [out_file]`
  - Default: `./scripts/show_results.sh /tmp/results18004.txt`

- `./scripts/diagnose_flow.sh [port] [pshome]`
  - Default: `./scripts/diagnose_flow.sh 18001 /tmp/pshome`

## Example

```bash
cd /mnt/c/Users/Brennan/psirver-bloi/src
./scripts/verify_professor_subset.sh
./scripts/verify_to_file.sh 18104 /tmp/pshome18104 /tmp/my_results.txt
./scripts/show_results.sh /tmp/my_results.txt
```

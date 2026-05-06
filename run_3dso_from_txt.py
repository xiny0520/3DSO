import argparse
from pathlib import Path
import subprocess
import tempfile


def default_executable(root: Path) -> Path:
    return root / "build_release_vs" / "Release" / "3dso.exe"


def main() -> None:
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="Run 3DSO for one .txt/.xyz point-cloud file.")
    parser.add_argument("point_file", type=Path, help="Input .txt or .xyz file with x y z columns")
    parser.add_argument("--executable", type=Path, default=default_executable(root))
    parser.add_argument("--output-csv", type=Path, default=root / "results" / "single_file.csv")
    parser.add_argument("--k-voxel", type=int, default=3)
    parser.add_argument("--voxel-size", type=float, default=0.1)
    parser.add_argument("--block-ratio", type=int, default=5)
    parser.add_argument("--plot-size", nargs=3, default=["1", "25", "25"], metavar=("MODE", "X", "Y"))
    parser.add_argument("--threads", type=int, default=1)
    args = parser.parse_args()

    if not args.point_file.exists():
        raise FileNotFoundError(args.point_file)
    if not args.executable.exists():
        raise FileNotFoundError(f"Executable not found: {args.executable}")

    # The CLI processes directories, so create a temporary directory containing one file link/copy.
    with tempfile.TemporaryDirectory(prefix="sc3d_single_") as tmp:
        tmp_dir = Path(tmp)
        target = tmp_dir / args.point_file.name
        try:
            target.hardlink_to(args.point_file)
        except OSError:
            target.write_bytes(args.point_file.read_bytes())

        command = [
            str(args.executable),
            "--input-dir",
            str(tmp_dir),
            "--output-csv",
            str(args.output_csv),
            "--k-voxel",
            str(args.k_voxel),
            "--voxel-size",
            str(args.voxel_size),
            "--block-ratio",
            str(args.block_ratio),
            "--plot-size",
            *map(str, args.plot_size),
            "--threads",
            str(args.threads),
        ]
        subprocess.run(command, check=True)

    print(f"Wrote {args.output_csv}")


if __name__ == "__main__":
    main()

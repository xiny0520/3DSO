from pathlib import Path
import subprocess


def main() -> None:
    root = Path(__file__).resolve().parent
    exe = root / "build_release_vs" / "Release" / "3dso.exe"
    input_dir = root / "data"
    output_csv = root / "results" / "python_example.csv"

    command = [
        str(exe),
        "--input-dir",
        str(input_dir),
        "--output-csv",
        str(output_csv),
        "--k-voxel",
        "3",
        "--voxel-size",
        "0.3",
        "--block-ratio",
        "5",
        "--plot-size",
        "1",
        "25",
        "25",
        "--threads",
        "1",
        "--limit",
        "1",
    ]

    subprocess.run(command, check=True)
    print(f"Wrote {output_csv}")


if __name__ == "__main__":
    main()

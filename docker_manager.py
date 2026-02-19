#!/usr/bin/env python3
import argparse
import subprocess
import os
import shutil
import sys
import time
import platform

# --- Configuration ---
APP_IMAGE_TAG = "cherryrecorder-server:latest"
TEST_IMAGE_TAG = "cherryrecorder-server-test:latest"
APP_CONTAINER_NAME = "cherryrecorder-server-container"
APP_DOCKERFILE = "Dockerfile"
TEST_DOCKERFILE = "Dockerfile.test"
TEST_DOCKERIGNORE = "Dockerfile.test.dockerignore"
DEFAULT_DOCKERIGNORE = ".dockerignore"
BACKUP_DOCKERIGNORE = ".dockerignore.original"

# --- Architecture Detection ---
def get_current_arch():
    """Detect current system architecture"""
    machine = platform.machine().lower()
    if machine in ['x86_64', 'amd64']:
        return 'amd64'
    elif machine in ['aarch64', 'arm64']:
        return 'arm64'
    else:
        return machine

# --- Port Mapping (Host:Container) - Only for Application Mode ---
HOST_PORT_HTTP = "8080"
CONTAINER_PORT_HTTP = "8080"
HOST_PORT_CHAT = "33334"
CONTAINER_PORT_CHAT = "33334"

# --- Environment Variables File - Only for Application Mode ---
ENV_FILE_PATH = ".env.docker"
# ECS 환경에서 사용할 수 있는 대체 경로들
ENV_FILE_ALTERNATIVES = [".env", "server-config.env", "../server-config.env"]

# --- ECS t2.micro 환경 설정 ---
IS_T2_MICRO = os.environ.get('ECS_INSTANCE_TYPE', '').lower() == 't2.micro'

# --- Helper Functions ---
def run_command(command, check=True, env=None, **kwargs):
    """Runs a shell command and optionally checks for errors."""
    print(f"\n---> Running command: {' '.join(command)}")
    
    # Prepare environment variables
    cmd_env = os.environ.copy()
    if env:
        cmd_env.update(env)
    
    try:
        # Use shell=True on Windows if needed, but avoid if possible
        use_shell = sys.platform == "win32"
        # Pass the 'check' argument received by the function
        result = subprocess.run(command, check=check, text=True, shell=use_shell, env=cmd_env, **kwargs)
        print(f"---> Command successful.")
        return True
    except subprocess.CalledProcessError as e:
        # Only print error if check=True
        if check:
            print(f"ERROR: Command failed with exit code {e.returncode}", file=sys.stderr)
            print(f"ERROR details: {e}", file=sys.stderr)
        else:
            # Optionally log non-fatal errors when check=False
            print(f"---> Command finished with non-zero exit code {e.returncode} (check=False, ignored).")
        # Return False only if check=True caused the exception
        return not check
    except FileNotFoundError:
        print(f"ERROR: Command not found: {command[0]}", file=sys.stderr)
        print("Ensure Docker CLI is installed and in your PATH.", file=sys.stderr)
        return False

def prepare_dockerignore_for_test():
    """Backs up .dockerignore and replaces it with the test version if needed."""
    print("Preparing .dockerignore for TEST build...")
    backup_made = False
    # Cleanup any lingering backup
    if os.path.exists(BACKUP_DOCKERIGNORE):
        try:
            os.remove(BACKUP_DOCKERIGNORE)
        except OSError as e:
            print(f"WARNING: Could not remove lingering backup {BACKUP_DOCKERIGNORE}: {e}", file=sys.stderr)

    if not os.path.exists(TEST_DOCKERIGNORE):
        print(f"WARNING: {TEST_DOCKERIGNORE} not found. Build will use {DEFAULT_DOCKERIGNORE} (if it exists).")
        return False # Indicate no backup needed

    if os.path.exists(DEFAULT_DOCKERIGNORE):
        print(f"  Backing up {DEFAULT_DOCKERIGNORE} to {BACKUP_DOCKERIGNORE}")
        try:
            shutil.move(DEFAULT_DOCKERIGNORE, BACKUP_DOCKERIGNORE)
            backup_made = True
        except OSError as e:
            print(f"ERROR: Failed to backup {DEFAULT_DOCKERIGNORE}: {e}", file=sys.stderr)
            raise # Re-raise the exception to stop the process

    print(f"  Copying {TEST_DOCKERIGNORE} to {DEFAULT_DOCKERIGNORE}...")
    try:
        shutil.copy2(TEST_DOCKERIGNORE, DEFAULT_DOCKERIGNORE) # copy2 preserves metadata
    except OSError as e:
        print(f"ERROR: Failed to copy {TEST_DOCKERIGNORE} to {DEFAULT_DOCKERIGNORE}: {e}", file=sys.stderr)
        # Attempt to restore backup if one was made
        if backup_made:
            try:
                shutil.move(BACKUP_DOCKERIGNORE, DEFAULT_DOCKERIGNORE)
            except OSError as re:
                print(f"ERROR: Failed to restore backup {BACKUP_DOCKERIGNORE}: {re}", file=sys.stderr)
        raise # Re-raise the exception

    return backup_made

def restore_dockerignore(backup_made):
    """Restores the original .dockerignore file."""
    print("Restoring .dockerignore...")
    if backup_made:
        try:
            if os.path.exists(DEFAULT_DOCKERIGNORE):
                os.remove(DEFAULT_DOCKERIGNORE)
            shutil.move(BACKUP_DOCKERIGNORE, DEFAULT_DOCKERIGNORE)
            print(f"  Restored {DEFAULT_DOCKERIGNORE} from {BACKUP_DOCKERIGNORE}")
        except OSError as e:
            print(f"WARNING: Failed to restore {DEFAULT_DOCKERIGNORE} from backup: {e}", file=sys.stderr)
    else:
        # If we copied the test file but no original existed, remove the copied one
        # We check if the copied file *is* the test one by comparing content (more robust than just existence)
        copied_is_test = False
        if os.path.exists(DEFAULT_DOCKERIGNORE) and os.path.exists(TEST_DOCKERIGNORE):
            try:
                # Simple content check (may fail for large files, but okay for ignore files)
                 with open(DEFAULT_DOCKERIGNORE, 'r') as f1, open(TEST_DOCKERIGNORE, 'r') as f2:
                     if f1.read() == f2.read():
                         copied_is_test = True
            except IOError as e:
                 print(f"WARNING: Could not compare ignore files: {e}", file=sys.stderr)

        if copied_is_test:
             print(f"  Removing temporary {DEFAULT_DOCKERIGNORE} (copied from {TEST_DOCKERIGNORE})...")
             try:
                 os.remove(DEFAULT_DOCKERIGNORE)
             except OSError as e:
                 print(f"WARNING: Failed to remove temporary {DEFAULT_DOCKERIGNORE}: {e}", file=sys.stderr)


# --- Main Script Logic ---
def main():
    parser = argparse.ArgumentParser(description="Build and run/test CherryRecorder Server using Docker.")
    parser.add_argument(
        "--target",
        choices=["app", "test"],
        default="app",
        help="Target to build and run: 'app' (default) or 'test'."
    )
    parser.add_argument(
        "--platform",
        choices=["amd64", "arm64", "both", "auto"],
        default="auto",
        help="Platform to build for: 'amd64', 'arm64', 'both' (multi-arch), or 'auto' (detect current)."
    )
    parser.add_argument(
        "--push",
        action="store_true",
        help="Push multi-arch images to registry (requires --platform=both)"
    )
    args = parser.parse_args()
    
    # Platform 설정
    if args.platform == "auto":
        args.platform = get_current_arch()
        print(f"Auto-detected platform: {args.platform}")
    
    # Multi-arch push는 'both' platform에서만 가능
    if args.push and args.platform != "both":
        print("ERROR: --push requires --platform=both", file=sys.stderr)
        sys.exit(1)

    # Docker BuildKit 활성화를 위한 환경 변수
    build_env = {"DOCKER_BUILDKIT": "1"}
    
    backup_made = False
    try:
        # --- Prepare Environment based on Target ---
        if args.target == "test":
            print("--- Running in TEST mode ---")
            image_tag = TEST_IMAGE_TAG
            dockerfile = TEST_DOCKERFILE
            backup_made = prepare_dockerignore_for_test()
            # Platform 옵션 추가
            platform_opts = []
            if args.platform == "both":
                platform_opts = ["--platform", "linux/amd64,linux/arm64"]
            else:
                platform_opts = ["--platform", f"linux/{args.platform}"]
            
            build_args = ["docker", "buildx", "build"] + platform_opts + ["-t", image_tag, "-f", dockerfile]
            
            # Multi-arch build는 push 없이는 로컬에 로드할 수 없음
            if args.platform == "both":
                if args.push:
                    build_args.extend(["--push", "."])
                else:
                    print("WARNING: Multi-arch build without push will not load images locally")
                    build_args.append(".")
            else:
                build_args.extend(["--load", "."])
            
            run_args = ["docker", "run", "--rm", "-e", "GOOGLE_MAPS_API_KEY=dummy_key", image_tag]
        else: # app mode
            print("--- Running in APPLICATION mode ---")
            image_tag = APP_IMAGE_TAG
            dockerfile = APP_DOCKERFILE
            container_name = APP_CONTAINER_NAME

            # Stop and remove existing app container
            print(f"Stopping and removing existing container '{container_name}' (if any)...")
            run_command(["docker", "kill", container_name], stderr=subprocess.DEVNULL, check=False) # Ignore errors if container doesn't exist
            run_command(["docker", "rm", container_name], stderr=subprocess.DEVNULL, check=False)    # Ignore errors if container doesn't exist

            # Platform 옵션 추가
            platform_opts = []
            if args.platform == "both":
                platform_opts = ["--platform", "linux/amd64,linux/arm64"]
            else:
                platform_opts = ["--platform", f"linux/{args.platform}"]
            
            build_args = ["docker", "buildx", "build"] + platform_opts + ["-t", image_tag, "-f", dockerfile]
            
            # Multi-arch build는 push 없이는 로컬에 로드할 수 없음
            if args.platform == "both":
                if args.push:
                    build_args.extend(["--push", "."])
                else:
                    print("WARNING: Multi-arch build without push will not load images locally")
                    build_args.append(".")
            else:
                build_args.extend(["--load", "."])

            # Prepare run args for app
            run_args_list = ["docker", "run", "-d", "--name", container_name]
            
            # t2.micro 환경을 위한 리소스 제한
            # 1GB RAM 중 시스템용 100MB 제외하고 900MB 할당
            resource_limits = [
                "--memory", "900m",
                "--memory-swap", "900m",  # swap 비활성화 (성능 저하 방지)
                "--cpus", "0.95"  # CPU의 95% 사용 (시스템 여유분 확보)
            ]
            run_args_list.extend(resource_limits)
            
            # ECS t2.micro 환경에서 추가 설정
            if IS_T2_MICRO:
                ecs_options = [
                    "--privileged",  # WSL2/ECS libevent 문제 해결
                    "--log-driver", "awslogs",
                    "--log-opt", f"awslogs-group=/ecs/cherryrecorder-server",
                    "--log-opt", f"awslogs-region=ap-northeast-2",
                    "--log-opt", f"awslogs-stream=local-test",
                    "--restart", "unless-stopped"
                ]
                run_args_list.extend(ecs_options)
            else:
                # 로컬 개발 환경에서는 json-file 사용
                local_options = [
                    "--log-driver", "json-file",
                    "--log-opt", "max-size=10m",
                    "--log-opt", "max-file=3"
                ]
                run_args_list.extend(local_options)
            
            port_map_options = [
                "-p", f"{HOST_PORT_HTTP}:{CONTAINER_PORT_HTTP}",
                "-p", f"{HOST_PORT_CHAT}:{CONTAINER_PORT_CHAT}"
            ]
            run_args_list.extend(port_map_options)

            # 환경 변수 파일 찾기
            env_file_found = False
            for env_path in [ENV_FILE_PATH] + ENV_FILE_ALTERNATIVES:
                if os.path.exists(env_path):
                    print(f"Found environment file: {env_path}")
                    run_args_list.extend(["--env-file", env_path])
                    env_file_found = True
                    break
            
            if not env_file_found:
                print(f"WARNING: No environment file found. Checked: {[ENV_FILE_PATH] + ENV_FILE_ALTERNATIVES}")
                print("Container will run without environment file.")

            run_args_list.append(image_tag)
            run_args = run_args_list


        # --- Setup Docker Buildx (for multi-arch) ---
        if args.platform == "both" or args.platform == "arm64":
            print("Setting up Docker Buildx for multi-architecture build...")
            # Create and use buildx builder
            builder_name = "cherryrecorder-builder"
            run_command(["docker", "buildx", "create", "--name", builder_name, "--use"], check=False)
            run_command(["docker", "buildx", "inspect", "--bootstrap"])
        
        # --- Build Docker Image ---
        print("INFO: Docker BuildKit is enabled for improved caching performance.")
        print(f"INFO: Building for platform(s): {args.platform}")
        print("INFO: First build may take 20-30 minutes to compile all dependencies.")
        print("INFO: Subsequent builds will be much faster due to caching.")
        
        # t2.micro에서 빌드 시 메모리 부족 방지
        if args.target == "app":
            print("INFO: Building on t2.micro - using memory-optimized settings.")
            build_args.extend([
                "--build-arg", "VCPKG_MAX_CONCURRENCY=1",  # 병렬 빌드 제한
                # 빌드 시에는 메모리 제한 제거 (빌드 실패 방지)
                # "--memory", "900m",  
            ])
            
            # ARM64 빌드를 위한 추가 설정
            if args.platform in ["arm64", "both"]:
                build_args.extend([
                    "--build-arg", "ENABLE_ARM64_OPTIMIZATIONS=1"
                ])
        
        # 빌드 진행 상황을 표시하기 위해 --progress=plain 추가
        build_args.extend(["--progress=plain"])
        
        if not run_command(build_args, env=build_env):
            sys.exit(1) # Exit if build fails

        # --- Run Docker Container ---
        # Multi-arch build without push는 실행할 수 없음
        if args.platform == "both" and not args.push:
            print("\nMulti-arch build completed (not loaded locally).")
            print("To push images: re-run with --push flag")
        else:
            if not run_command(run_args):
                 print(f"ERROR: Docker run failed or tests failed for target '{args.target}'!", file=sys.stderr)
                 sys.exit(1) # Exit if run/tests fail

        # --- Post-run messages ---
        if args.target == "app":
            print(f"\nContainer '{APP_CONTAINER_NAME}' started successfully.")
            print(f"  To view logs: docker logs {APP_CONTAINER_NAME} -f")
            print(f"  To stop:      docker kill {APP_CONTAINER_NAME}")
            
            # 헬스체크 수행 (5초 대기 후)
            print("\nWaiting for container to be ready...")
            time.sleep(5)
            
            # 컨테이너 상태 확인
            print("\nContainer status:")
            check_cmd = ["docker", "ps", "--filter", f"name={APP_CONTAINER_NAME}", "--format", "table {{.Status}}"]
            run_command(check_cmd)
            
            # 헬스체크 엔드포인트 테스트
            print("\nTesting health endpoint...")
            health_cmd = ["docker", "exec", APP_CONTAINER_NAME, "curl", "-f", "http://localhost:8080/health"]
            if run_command(health_cmd, check=False):
                print("✓ Health check passed!")
            else:
                print("✗ Health check failed - server may still be starting up.")
                print("  Check logs with: docker logs " + APP_CONTAINER_NAME)
            
            # t2.micro 환경에서 리소스 사용량 표시
            if IS_T2_MICRO:
                print("\n--- Resource Usage (t2.micro) ---")
                stats_cmd = ["docker", "stats", "--no-stream", "--format", "table {{.Container}}\t{{.CPUPerc}}\t{{.MemUsage}}", APP_CONTAINER_NAME]
                run_command(stats_cmd)
        else:
            print("\nTests completed successfully.")

        print("\nScript finished.")

    except Exception as e:
        print(f"\nAn unexpected error occurred: {e}", file=sys.stderr)
        sys.exit(1)
    finally:
        # --- Ensure .dockerignore is restored ---
        if args.target == "test": # Only restore if we potentially modified it
             restore_dockerignore(backup_made)

if __name__ == "__main__":
    main() 
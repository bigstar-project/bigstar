use anyhow::Result;
use bigstar_rom::{generate_stable_roms, StableRomOptions};
use clap::{Parser, Subcommand, ValueEnum};
use std::path::PathBuf;

#[derive(Parser)]
#[command(author, version, about)]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    GenerateStable {
        #[arg(long)]
        source_rom: PathBuf,
        #[arg(long)]
        host_rom: PathBuf,
        #[arg(long)]
        client_rom: PathBuf,
        #[arg(long, default_value_t = 0)]
        stage: u8,
        #[arg(long, default_value_t = 2)]
        wins: u8,
        #[arg(long, default_value_t = 5)]
        big_stars: u8,
        #[arg(long, default_value = "endless")]
        lives: String,
        #[arg(long, value_enum, default_value_t = CourseMode::Random)]
        course_mode: CourseMode,
        #[arg(long)]
        scene_settings: Option<String>,
        #[arg(long, default_value = "tools/bigstar-rom/resources/symbols9.x")]
        symbols: PathBuf,
    },
}

#[derive(Clone, Copy, ValueEnum)]
enum CourseMode {
    Random,
    Select,
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    match cli.command {
        Command::GenerateStable {
            source_rom,
            host_rom,
            client_rom,
            stage,
            wins,
            big_stars,
            lives,
            course_mode,
            scene_settings,
            symbols,
        } => {
            let options = StableRomOptions {
                source_rom,
                host_rom,
                client_rom,
                stage,
                wins,
                big_stars,
                lives,
                course_mode: match course_mode {
                    CourseMode::Random => "random".to_owned(),
                    CourseMode::Select => "select".to_owned(),
                },
                scene_settings,
                symbols,
            };
            let result = generate_stable_roms(&options)?;
            println!("wrote stable host ROM: {}", result.host_rom.display());
            println!(
                "wrote stable client local1 ROM: {}",
                result.client_rom.display()
            );
        }
    }
    Ok(())
}

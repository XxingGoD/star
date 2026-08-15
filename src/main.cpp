import std;
import star.cli;

int main(int argc, char** argv) {
    std::vector<std::string_view> args;
    args.reserve(static_cast<std::size_t>(std::max(argc - 1, 0)));
    for (int index = 1; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }

    try {
        return star::cli::run(args, std::cout, std::cerr);
    } catch (const std::exception& error) {
        std::cerr << "error: internal failure: " << error.what() << '\n';
        return 1;
    }
}

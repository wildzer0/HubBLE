# Contributing

- Branch from `main`; one branch per change (`milestone/m<N>-<name>` for milestones).
- Use [Conventional Commits](https://www.conventionalcommits.org/)
  (`feat(ble): ...`, `fix(ota): ...`, `docs: ...`).
- Code, comments, docs and commit messages are written in English.
- Firmware: C17, ESP-IDF style, `clang-format`, `-Wall -Wextra -Werror`;
  document every function (Doxygen).
- Web app: TypeScript strict, ESLint + Prettier; document with TSDoc.
- Add or update tests and `CHANGELOG.md` with every pull request.
- Never commit secrets, keys or credentials.

See [docs/development.md](docs/development.md) for build and test commands.

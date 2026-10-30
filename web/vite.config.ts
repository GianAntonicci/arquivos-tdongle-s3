import { defineConfig } from 'vite';

// GitHub Pages publica em https://<usuario>.github.io/<repositorio>/
// O base precisa ser o nome do repositório; em `npm run dev` fica na raiz.
export default defineConfig(({ command }) => ({
  base: command === 'build' ? '/arquivos-tdongle-s3/' : '/',
  build: {
    target: 'es2022',
  },
  server: {
    host: 'localhost', // Web Serial exige contexto seguro: localhost vale
    port: 5173,
  },
}));

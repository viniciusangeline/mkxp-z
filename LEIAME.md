# DS Overworld v0.3 — prédios 3D de verdade no Pokémon Essentials v21.1

Duas peças, que funcionam juntas:

1. **Patch do motor** (`mkxp-z-2.4.2-model-addon.patch`): adiciona ao mkxp-z a classe `Model`, que carrega um `.obj` e o desenha com a GPU (com buffer de profundidade) dentro de um `Bitmap`.
2. **Plugin** (`DS_Overworld_v0.3_plugin.zip`): chão em perspectiva + prédios 3D posicionados por eventos.

O patch é feito para o mkxp-z **2.4.2 (commit c9378cf)**, o mesmo que acompanha o Essentials v21.1. Qualquer projeto v21.1 pode usar: basta trocar o `Game.exe` e copiar o plugin.

## Parte 1 — gerar o Game.exe com o patch

Você precisa de uma conta no GitHub (grátis). O GitHub compila para você; a primeira vez leva mais de 1 hora (compila todas as dependências), as seguintes são bem mais rápidas.

1. Faça um *fork* de https://github.com/mkxp-z/mkxp-z para a sua conta.
2. Num terminal, com o git instalado:
   ```
   git clone https://github.com/SEU_USUARIO/mkxp-z
   cd mkxp-z
   git checkout -b dsmodel c9378cf
   git apply mkxp-z-2.4.2-model-addon.patch
   git add -A
   git commit -m "Model addon"
   git push origin dsmodel
   ```
   (Se o `git apply` reclamar de espaços em branco/fim de linha, use `git apply --ignore-whitespace`.)
3. No GitHub, aba **Actions** → **Build Windows (Model addon)** → *Run workflow* (ou ela roda sozinha com o push).
4. Quando terminar, baixe o artefato `mkxp-z-windows-model-addon`.
5. **Faça backup** do `Game.exe` do seu projeto. Copie o `mkxp-z.exe` do artefato para a pasta do jogo e renomeie para `Game.exe`. Se o jogo reclamar de DLL, copie também as `.dll` do artefato. Mantenha o seu `mkxp.json`.
6. O Essentials pode mostrar um aviso amarelo de versão do mkxp-z (o hash do commit muda). É só um aviso.

## Parte 2 — plugin

1. Extraia `DS_Overworld_v0.3_plugin.zip` na raiz do projeto (apague a versão antiga em `Plugins/DS Overworld/`).
2. Rode o jogo em modo debug. Na janela de saída deve aparecer:
   `DS Overworld: Model self-test OK`
   Se aparecer "classe Model NÃO encontrada", o `Game.exe` ainda é o antigo.

### Colocando um prédio no mapa

1. Coloque `pokecenter.obj`, `pokecenter.mtl` e as texturas `.png` em `Graphics/Models/`.
2. No mapa, crie um evento **sem gráfico** com o nome `3D: pokecenter`.
   O evento marca a **frente** do prédio: o meio da parede da frente fica rente à borda de baixo do tile do evento, e o prédio cresce para o norte.
3. Opções (no nome do evento): `3D: pokecenter yaw=90 scale=2 dx=0 dz=0 dy=0 anchor=front`
   - `yaw`: rotação em graus (0 = frente para o sul)
   - `scale`: pixels do mapa por unidade do modelo (padrão 2 = 16 unidades por tile)
   - `dx`, `dz`: deslocamento em pixels (leste / sul); `dy`: altura do chão
   - `anchor`: `front` (padrão), `center` ou `origin`
4. A colisão é a do mapa: pinte a área do prédio com tiles intransponíveis.

### Limite de polígonos
`DSOverworld::MODEL_POLY_LIMIT` (padrão 30000 triângulos por mapa). Modelos que estouram o limite são ignorados, com aviso na janela de debug. Um Pokémon Center de Black/White tem 198 triângulos.

### Comandos (evento > Script)
```ruby
DSOverworld::Models.report      # lista prédios, triângulos e uso do limite
DSOverworld::Models.self_test   # confere se o Game.exe tem a classe Model
DSOverworld.camera.set(pitch: 45, zoom: 1.2)
DSOverworld.enable(false)       # volta ao 2D normal
```

## O que foi e o que NÃO foi testado

Testado aqui:
- Todos os arquivos C++ do patch compilam (checagem de sintaxe, sem avisos) contra o código-fonte exato do mkxp-z c9378cf, e o patch aplica limpo nesse commit.
- O parser de `.obj` (código real, isolado) reproduz as contagens do seu Pokémon Center: 386 vértices, 198 triângulos, 3 materiais.
- Os shaders reais foram executados em WebGL com o seu modelo: orientação, texturas, profundidade e perspectiva corretas, e seis pontos do chão caem exatamente nos pixels previstos pela fórmula do plugin.
- A lógica Ruby (cache, orçamento, erros, re-render só quando a câmera ou o jogador mexe) foi testada com um motor simulado.

**Não testado:** a compilação completa e a execução dentro do mkxp-z (não consigo compilar o `.exe` do Windows neste ambiente), nem o workflow do GitHub. As partes de maior risco são a integração com o estado do OpenGL do motor e as dependências do CI (de 2023; atualizei as versões de actions obsoletas). Se algo falhar, me mande a mensagem de erro ou o log do Actions e eu corrijo.

## Limitações desta versão
- Sombras no chão (materiais `kage`/`shadow`) são ocultadas, ainda não são desenhadas.
- Água e autotiles animados do chão ficam parados.
- Copas e telhados de tiles continuam achatados no chão (use modelos 3D para isso).
- Mapas conectados não desenham o vizinho nas bordas.
- A caixa usada para recortar o prédio é conservadora (um pouco maior que a silhueta).
- A câmera ainda não gira (o patch já aceita `yaw` por modelo; a rotação da câmera entra na próxima etapa).

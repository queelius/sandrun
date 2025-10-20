# Deploying Sandrun Documentation

This guide explains how to deploy the MkDocs documentation to GitHub Pages.

## GitHub Pages Deployment Options

### Option 1: Automated GitHub Actions (Recommended)

The repository includes a GitHub Actions workflow that automatically builds and deploys documentation on every push to master.

**Setup:**

1. **Enable GitHub Pages in your repository settings:**
   - Go to Settings → Pages
   - Source: "GitHub Actions"
   - Save

2. **Push your changes:**
   ```bash
   git push origin master
   ```

3. **Wait for deployment:**
   - Go to Actions tab to watch the build
   - Documentation will be live at: `https://yourusername.github.io/sandrun/`

**Workflow File:** `.github/workflows/deploy-docs.yml`

The workflow:
- Triggers on push to master/main
- Installs Python and MkDocs dependencies
- Builds the site with `mkdocs build --strict`
- Deploys to GitHub Pages

### Option 2: Manual Deployment with mkdocs

**Prerequisites:**
```bash
pip install mkdocs-material pymdown-extensions mkdocs-minify-plugin
```

**Deploy:**
```bash
# Build and deploy in one command
mkdocs gh-deploy

# This will:
# 1. Build the site to ./site/
# 2. Push to gh-pages branch
# 3. Documentation goes live at https://yourusername.github.io/sandrun/
```

**Important:** Make sure GitHub Pages is configured to use the `gh-pages` branch:
- Settings → Pages → Source → Branch: gh-pages

### Option 3: Build Locally and Deploy Manually

```bash
# Build the site
mkdocs build

# The static site is now in ./site/
# You can deploy this to any static hosting:
# - Netlify
# - Vercel
# - AWS S3
# - Your own server
```

## Troubleshooting

### Error: "No such file or directory @ dir_chdir0 - /github/workspace/docs"

**Problem:** GitHub Pages is trying to use Jekyll instead of MkDocs.

**Solution:** The `.nojekyll` file in the `docs/` directory tells GitHub to skip Jekyll processing. Make sure:

1. `docs/.nojekyll` exists (empty file)
2. GitHub Pages is set to use "GitHub Actions" as the source (not "Deploy from a branch")

### Error: "Theme 'material' not found"

**Problem:** MkDocs Material theme not installed.

**Solution:**
```bash
pip install mkdocs-material
```

Or use the GitHub Actions workflow which installs it automatically.

### Error: "Documentation failed to build"

**Problem:** Broken links or invalid markdown.

**Solution:**
```bash
# Build with strict mode to see all warnings
mkdocs build --strict

# Fix any broken links or formatting issues
```

### Documentation Not Updating

**Problem:** Changes not showing on GitHub Pages.

**Solutions:**

1. **Clear browser cache:**
   - Hard refresh: Ctrl+F5 (Windows/Linux) or Cmd+Shift+R (Mac)

2. **Check GitHub Actions:**
   - Go to Actions tab
   - Verify the latest run succeeded
   - Check deployment logs

3. **Verify gh-pages branch:**
   ```bash
   git fetch origin gh-pages
   git log origin/gh-pages
   ```

4. **Wait for GitHub Pages propagation:**
   - Can take 1-5 minutes after deployment

## Custom Domain

To use a custom domain (e.g., `docs.sandrun.io`):

1. **Create CNAME file:**
   ```bash
   echo "docs.sandrun.io" > docs/CNAME
   git add docs/CNAME
   git commit -m "Add custom domain"
   ```

2. **Configure DNS:**
   - Add CNAME record: `docs` → `yourusername.github.io`

3. **Enable in GitHub:**
   - Settings → Pages → Custom domain: `docs.sandrun.io`
   - Check "Enforce HTTPS"

## Local Preview

Before deploying, preview locally:

```bash
# Start development server
mkdocs serve

# Open in browser
open http://localhost:8000

# Make changes and see them live (auto-reload)
```

## Build Configuration

Key settings in `mkdocs.yml`:

```yaml
site_name: Sandrun Documentation
site_url: https://yourusername.github.io/sandrun/  # Update this!

theme:
  name: material
  palette:
    - scheme: default
      toggle:
        icon: material/brightness-7
        name: Switch to dark mode
    - scheme: slate
      toggle:
        icon: material/brightness-4
        name: Switch to light mode
```

## Versioned Documentation (Advanced)

For versioned documentation (e.g., v1.0, v2.0):

```bash
# Install mike
pip install mike

# Deploy version
mike deploy 1.0 latest --update-aliases
mike set-default latest

# Push to gh-pages
git push origin gh-pages
```

Then users can switch between versions in the documentation.

## Analytics (Optional)

To add Google Analytics:

1. **Get Google Analytics ID** (e.g., `G-XXXXXXXXXX`)

2. **Update mkdocs.yml:**
   ```yaml
   extra:
     analytics:
       provider: google
       property: G-XXXXXXXXXX  # Your actual ID
   ```

3. **Redeploy documentation**

## Maintenance

### Updating Documentation

1. **Edit markdown files** in `docs/`
2. **Preview changes:** `mkdocs serve`
3. **Commit and push:**
   ```bash
   git add docs/
   git commit -m "docs: Update getting started guide"
   git push origin master
   ```
4. **GitHub Actions automatically deploys**

### Updating Dependencies

Keep MkDocs and plugins updated:

```bash
pip install --upgrade mkdocs-material pymdown-extensions mkdocs-minify-plugin
mkdocs build --strict  # Verify still works
```

## CI/CD Integration

The GitHub Actions workflow can be extended to:

- Run on pull requests (preview deployments)
- Validate links before deploying
- Generate sitemap and robots.txt
- Optimize images
- Run spell check

Example addition to workflow:

```yaml
- name: Check links
  run: |
    pip install linkchecker
    linkchecker site/
```

## Support

If you encounter issues:

1. **Check build logs** in GitHub Actions
2. **Verify mkdocs.yml syntax** with `mkdocs build --strict`
3. **Test locally first** with `mkdocs serve`
4. **Read MkDocs docs:** https://www.mkdocs.org/
5. **Material theme docs:** https://squidfunk.github.io/mkdocs-material/

## See Also

- [MkDocs Documentation](https://www.mkdocs.org/)
- [Material for MkDocs](https://squidfunk.github.io/mkdocs-material/)
- [GitHub Pages Documentation](https://docs.github.com/en/pages)
